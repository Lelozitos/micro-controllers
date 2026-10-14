// Bridges the race timer's MQTT feed to the browser over Socket.IO.
//
// The C++ race timer (../main.cpp via mqtt_publisher.cpp) publishes to:
//   race/state  -> {"state":"running","started_at":<epoch_ms>} | {"state":"idle"}
//   race/state  -> {"state":"countdown","lit":N} while the start lights come on
//                  {"state":"running",...,"laps":N} / {"state":"finished","winner":...}
//   race/lap    -> {"tag":"A1B2C3D4","racer":"Name","lap":2,"lap_time_s":31.2,"elapsed_s":63.0}
//   race/finish -> {"place":N,"tag":"A1B2C3D4","racer":"Name","laps":5,"elapsed_s":160.4}
//   race/config -> {"led_order":[...],"led_pins":[...],"laps":N}  (retained)
//   race/timer  -> "online" | "offline"                   (retained, last will)
//
// and listens on race/command for control from this dashboard:
//   {"cmd":"start"} {"cmd":"reset"} {"cmd":"set_led_order","order":[2,0,1]}
//   {"cmd":"test_led","led":1} {"cmd":"set_laps","laps":5}
//
// Point MQTT_HOST at a local Mosquitto broker running on this machine (or the
// Pi) for a fully offline setup, or at the same broker the race timer's
// config.hpp already targets.

const path = require('path');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mqtt = require('mqtt');

const MQTT_HOST = process.env.MQTT_HOST || 'localhost';
const MQTT_PORT = process.env.MQTT_PORT || 1883;
const MQTT_TOKEN = process.env.MQTT_TOKEN || ''; // set if your broker needs auth (e.g. Ubidots token)
const HTTP_PORT = process.env.PORT || 3000;

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

const server = http.createServer(app);
const io = new Server(server);

let lastState = { state: 'idle' };
// Per-racer progress for the current race, keyed by tag; cleared on each start.
let racers = new Map();
let lastConfig = null;
let timerOnline = false;

const mqttClient = mqtt.connect(`mqtt://${MQTT_HOST}:${MQTT_PORT}`, {
  username: MQTT_TOKEN || undefined,
  password: MQTT_TOKEN ? '' : undefined,
  reconnectPeriod: 2000,
});

mqttClient.on('connect', () => {
  console.log(`[mqtt] connected to ${MQTT_HOST}:${MQTT_PORT}`);
  mqttClient.subscribe(['race/state', 'race/lap', 'race/finish', 'race/config', 'race/timer'], (err) => {
    if (err) console.error('[mqtt] subscribe failed', err);
  });
});

mqttClient.on('reconnect', () => console.log('[mqtt] reconnecting...'));
mqttClient.on('error', (err) => console.error('[mqtt] error', err.message));

mqttClient.on('message', (topic, payload) => {
  if (topic === 'race/timer') {
    timerOnline = payload.toString() === 'online';
    io.emit('timer', timerOnline);
    return;
  }

  let data;
  try {
    data = JSON.parse(payload.toString());
  } catch (e) {
    console.error('[mqtt] bad payload on', topic, payload.toString());
    return;
  }

  if (topic === 'race/state') {
    lastState = data;
    if (data.state === 'running' || data.state === 'idle') {
      racers = new Map(); // fresh race
      io.emit('standings', standings());
    }
    io.emit('state', data);
  } else if (topic === 'race/lap') {
    const r = racers.get(data.tag) || { tag: data.tag, racer: data.racer, laps: 0, best_lap_s: null };
    r.racer = data.racer;
    r.laps = data.lap;
    r.last_lap_s = data.lap_time_s;
    r.best_lap_s = r.best_lap_s == null ? data.lap_time_s : Math.min(r.best_lap_s, data.lap_time_s);
    r.elapsed_s = data.elapsed_s;
    racers.set(data.tag, r);
    io.emit('standings', standings());
  } else if (topic === 'race/finish') {
    const r = racers.get(data.tag);
    if (r) r.place = data.place;
    io.emit('standings', standings());
  } else if (topic === 'race/config') {
    lastConfig = data;
    io.emit('config', data);
  }
});

// Classified racers first (by finishing place), then by most laps, then by
// who completed that many laps first.
function standings() {
  return [...racers.values()].sort((a, b) => {
    if (a.place && b.place) return a.place - b.place;
    if (a.place || b.place) return a.place ? -1 : 1;
    if (a.laps !== b.laps) return b.laps - a.laps;
    return a.elapsed_s - b.elapsed_s;
  });
}

// A browser opening the page late still gets the current picture.
io.on('connection', (socket) => {
  socket.emit('timer', timerOnline);
  socket.emit('state', lastState);
  socket.emit('standings', standings());
  if (lastConfig) socket.emit('config', lastConfig);

  // Race control from the browser -> race timer. Only known commands with
  // sane arguments are forwarded; the timer validates again.
  socket.on('command', (msg) => {
    if (!msg || typeof msg.cmd !== 'string') return;
    let out;
    if (msg.cmd === 'start' || msg.cmd === 'reset') {
      out = { cmd: msg.cmd };
    } else if (msg.cmd === 'set_led_order' && Array.isArray(msg.order) &&
               msg.order.every(Number.isInteger)) {
      out = { cmd: msg.cmd, order: msg.order };
    } else if (msg.cmd === 'set_laps' && Number.isInteger(msg.laps) && msg.laps >= 1 && msg.laps <= 99) {
      out = { cmd: msg.cmd, laps: msg.laps };
    } else if (msg.cmd === 'test_led' && Number.isInteger(msg.led)) {
      out = { cmd: msg.cmd, led: msg.led };
    } else {
      return;
    }
    mqttClient.publish('race/command', JSON.stringify(out), { qos: 1 });
  });
});

server.listen(HTTP_PORT, () => {
  console.log(`Dashboard: http://localhost:${HTTP_PORT}`);
});
