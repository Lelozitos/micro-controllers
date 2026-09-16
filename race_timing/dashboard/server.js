// Bridges the race timer's MQTT feed to the browser over Socket.IO.
//
// The C++ race timer (../main.cpp via mqtt_publisher.cpp) publishes to:
//   race/state  -> {"state":"running","started_at":<epoch_ms>} | {"state":"idle"}
//   race/finish -> {"place":N,"racer":"Name","elapsed_s":12.4}
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
let leaderboard = []; // cleared on each race start, appended on each finish

const mqttClient = mqtt.connect(`mqtt://${MQTT_HOST}:${MQTT_PORT}`, {
  username: MQTT_TOKEN || undefined,
  password: MQTT_TOKEN ? '' : undefined,
  reconnectPeriod: 2000,
});

mqttClient.on('connect', () => {
  console.log(`[mqtt] connected to ${MQTT_HOST}:${MQTT_PORT}`);
  mqttClient.subscribe(['race/state', 'race/finish'], (err) => {
    if (err) console.error('[mqtt] subscribe failed', err);
  });
});

mqttClient.on('reconnect', () => console.log('[mqtt] reconnecting...'));
mqttClient.on('error', (err) => console.error('[mqtt] error', err.message));

mqttClient.on('message', (topic, payload) => {
  let data;
  try {
    data = JSON.parse(payload.toString());
  } catch (e) {
    console.error('[mqtt] bad payload on', topic, payload.toString());
    return;
  }

  if (topic === 'race/state') {
    lastState = data;
    if (data.state === 'running') leaderboard = []; // fresh race
    io.emit('state', data);
  } else if (topic === 'race/finish') {
    leaderboard.push(data);
    io.emit('finish', data);
  }
});

// A browser opening the page late still gets the current picture.
io.on('connection', (socket) => {
  socket.emit('state', lastState);
  socket.emit('snapshot', leaderboard);
});

server.listen(HTTP_PORT, () => {
  console.log(`Dashboard: http://localhost:${HTTP_PORT}`);
});
