// Bridges the race timer's MQTT feed to the browser over Socket.IO, and keeps
// the full timing record of each race (every lap and sector) so it can be
// shown live, analysed per driver and printed afterwards.
//
// The C++ race timer (../main.cpp via mqtt_publisher.cpp) publishes to:
//   race/state  -> {"state":"idle"} | {"state":"countdown","lit":N}
//                  {"state":"running","started_at":<epoch_ms>,"laps":N,"sectors":S}
//                  {"state":"finished","winner":"Name","elapsed_s":160.4,"laps":N}
//   race/start  -> {"tag":"A1B2C3D4","racer":"Name","elapsed_s":4.2}   (first finish crossing: lap 1 starts)
//   race/sector -> {"tag":"A1B2C3D4","racer":"Name","lap":2,"sector":1,"sector_time_s":10.4,"elapsed_s":41.6}
//   race/lap    -> {"tag":"A1B2C3D4","racer":"Name","lap":2,"lap_time_s":31.2,"elapsed_s":63.0,"sectors":[10.4,null,11.0]}
//   race/finish -> {"place":N,"tag":"A1B2C3D4","racer":"Name","laps":5,"elapsed_s":160.4}
//   race/tag    -> {"checkpoint":0,"tag":"A1B2C3D4","racer":"Name"}   (every tag presented)
//   race/config -> {"led_order":[...],"led_pins":[...],"laps":N,"checkpoints":[...],
//                   "spi_devices":[...],"racers":[...]}               (retained)
//   race/timer  -> "online" | "offline"                                (retained, last will)
//
// and listens on race/command for control from this dashboard:
//   {"cmd":"start"} {"cmd":"reset"} {"cmd":"set_laps","laps":5}
//   {"cmd":"set_led_order","order":[2,0,1]} {"cmd":"test_led","led":1}
//   {"cmd":"set_checkpoints","devices":["/dev/spidev0.0","/dev/spidev0.1"]}
//   {"cmd":"sim_tag","checkpoint":1,"tag":"A1B2C3D4"} {"cmd":"reload_racers"}
//   {"cmd":"set_racers","racers":[{"tag":"A1B2C3D4","name":"Alice"}]}
//
// Finished races are saved as JSON in ./results, one file per race.

const fs = require('fs');
const path = require('path');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mqtt = require('mqtt');

const MQTT_HOST = process.env.MQTT_HOST || 'localhost';
const MQTT_PORT = process.env.MQTT_PORT || 1883;
const MQTT_TOKEN = process.env.MQTT_TOKEN || ''; // set if your broker needs auth (e.g. Ubidots token)
const HTTP_PORT = process.env.PORT || 3000;
const RESULTS_DIR = process.env.RESULTS_DIR || path.join(__dirname, 'results');

fs.mkdirSync(RESULTS_DIR, { recursive: true });

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

const server = http.createServer(app);
const io = new Server(server);

let lastState = { state: 'idle' };
let lastConfig = null;
let timerOnline = false;
let tagLog = []; // recent reads at any line, newest first, for the developer tab

// ---- race record ----------------------------------------------------------
//
// race = {
//   id, started_at, laps, sectors, state: running|finished|closed, winner,
//   drivers: { <tag>: {
//     tag, name, place, start_s,             // race time lap 1 started
//     laps: [{ lap, time, sectors: [s|null], elapsed }],
//     current: { lap, sectors: [s|null] },   // lap in progress
//     splits: { <P>: elapsed },              // P = timing lines passed since GO
//   } }
// }
//
// P counts every line crossed: with L lines per lap (L = sectors, or 1 with
// only a finish line), the start crossing is 0, crossing sector line k on
// lap n is (n-1)*L + k and finishing lap n is n*L. It orders drivers on track and gives live gaps.
let race = null;

function linesPerLap(r) {
  return r.sectors > 0 ? r.sectors : 1;
}

function driver(tag, name) {
  let d = race.drivers[tag];
  if (!d) {
    d = race.drivers[tag] = {
      tag, name, place: null, start_s: null, laps: [],
      current: { lap: 1, sectors: Array(race.sectors).fill(null) },
      splits: {},
    };
  }
  if (name) d.name = name;
  return d;
}

function hasTimes(r) {
  return r && Object.values(r.drivers).some((d) => Object.keys(d.splits).length);
}

function raceFile(id) {
  return path.join(RESULTS_DIR, `${id}.json`);
}

// Saved on every change (a few hundred bytes per lap), so nothing is lost if
// the dashboard stops mid-race.
let saveTimer = null;
function saveRace() {
  if (!race || !hasTimes(race)) return;
  const r = race;
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    fs.writeFile(raceFile(r.id), JSON.stringify(r), (err) => {
      if (err) console.error('[results] save failed', err.message);
      else broadcastHistory();
    });
  }, 500);
}

// The race stays as `race` (state closed) so the Live tab can keep showing
// the final standings until the next start; it's no longer the current race.
function closeRace() {
  if (!race || race.state === 'closed') return;
  race.state = 'closed';
  if (hasTimes(race)) {
    clearTimeout(saveTimer);
    try {
      fs.writeFileSync(raceFile(race.id), JSON.stringify(race));
    } catch (err) {
      console.error('[results] save failed', err.message);
    }
  }
  broadcastHistory();
}

function isLive(r) {
  return r && r.state !== 'closed';
}

function raceSummary(r) {
  const drivers = Object.values(r.drivers);
  const winner = drivers.find((d) => d.place === 1);
  return {
    id: r.id, started_at: r.started_at, laps: r.laps, sectors: r.sectors,
    drivers: drivers.length, winner: winner ? winner.name : null,
  };
}

function history() {
  let files = [];
  try {
    files = fs.readdirSync(RESULTS_DIR).filter((f) => /^\d+\.json$/.test(f));
  } catch (err) {
    return [];
  }
  const list = [];
  for (const f of files) {
    try {
      list.push(raceSummary(JSON.parse(fs.readFileSync(path.join(RESULTS_DIR, f), 'utf8'))));
    } catch (err) {
      console.error('[results] unreadable', f);
    }
  }
  return list.sort((a, b) => b.started_at - a.started_at);
}

function broadcastHistory() {
  io.emit('history', history());
}

function broadcastRace() {
  io.emit('race', race);
}

// ---- MQTT -----------------------------------------------------------------

const mqttClient = mqtt.connect(`mqtt://${MQTT_HOST}:${MQTT_PORT}`, {
  username: MQTT_TOKEN || undefined,
  password: MQTT_TOKEN ? '' : undefined,
  reconnectPeriod: 2000,
});

mqttClient.on('connect', () => {
  console.log(`[mqtt] connected to ${MQTT_HOST}:${MQTT_PORT}`);
  mqttClient.subscribe(
    ['race/state', 'race/start', 'race/lap', 'race/sector', 'race/finish', 'race/tag', 'race/config', 'race/timer'],
    (err) => { if (err) console.error('[mqtt] subscribe failed', err); },
  );
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
    if (data.state === 'running') {
      closeRace();
      race = {
        id: String(data.started_at || Date.now()),
        started_at: data.started_at || Date.now(),
        laps: data.laps || 1,
        sectors: data.sectors || 0,
        state: 'running',
        winner: null,
        drivers: {},
      };
      broadcastRace();
    } else if (data.state === 'finished' && race) {
      race.state = 'finished';
      race.winner = data.winner || null;
      saveRace();
      broadcastRace();
    } else if (data.state === 'idle') {
      closeRace();
      broadcastRace();
    }
    io.emit('state', data);
  } else if (topic === 'race/start' && race) {
    const d = driver(data.tag, data.racer);
    d.start_s = data.elapsed_s;
    d.current = { lap: 1, sectors: Array(race.sectors).fill(null) };
    d.splits[0] = data.elapsed_s;
    saveRace();
    broadcastRace();
  } else if (topic === 'race/sector' && race) {
    const d = driver(data.tag, data.racer);
    if (d.current.lap !== data.lap) d.current = { lap: data.lap, sectors: Array(race.sectors).fill(null) };
    d.current.sectors[data.sector - 1] = data.sector_time_s;
    d.splits[(data.lap - 1) * linesPerLap(race) + data.sector] = data.elapsed_s;
    saveRace();
    broadcastRace();
  } else if (topic === 'race/lap' && race) {
    const d = driver(data.tag, data.racer);
    const sectors = Array.isArray(data.sectors) ? data.sectors : [];
    d.laps.push({ lap: data.lap, time: data.lap_time_s, sectors, elapsed: data.elapsed_s });
    d.current = { lap: data.lap + 1, sectors: Array(race.sectors).fill(null) };
    d.splits[data.lap * linesPerLap(race)] = data.elapsed_s;
    saveRace();
    broadcastRace();
  } else if (topic === 'race/finish' && race) {
    driver(data.tag, data.racer).place = data.place;
    saveRace();
    broadcastRace();
  } else if (topic === 'race/tag') {
    const entry = { at: Date.now(), checkpoint: data.checkpoint, tag: data.tag, racer: data.racer };
    tagLog = [entry, ...tagLog].slice(0, 50);
    io.emit('tag', entry);
  } else if (topic === 'race/config') {
    lastConfig = data;
    io.emit('config', data);
  }
});

// ---- browser --------------------------------------------------------------

const DEVICE_RE = /^\/dev\/spidev\d+\.\d+$/;

// A browser opening the page late still gets the current picture.
io.on('connection', (socket) => {
  socket.emit('server_time', Date.now()); // the browser's clock may differ from the Pi's
  socket.emit('timer', timerOnline);
  socket.emit('state', lastState);
  socket.emit('race', race);
  socket.emit('history', history());
  socket.emit('tags', tagLog);
  if (lastConfig) socket.emit('config', lastConfig);

  socket.on('get_race', (id, reply) => {
    if (typeof reply !== 'function') return;
    if (isLive(race) && race.id === id) return reply(race);
    if (!/^\d+$/.test(String(id))) return reply(null);
    fs.readFile(raceFile(id), 'utf8', (err, text) => {
      if (err) return reply(null);
      try { reply(JSON.parse(text)); } catch (e) { reply(null); }
    });
  });

  socket.on('delete_race', (id) => {
    if (!/^\d+$/.test(String(id)) || (isLive(race) && race.id === id)) return;
    if (race && race.id === id) {
      race = null; // the ended race still on the Live tab
      broadcastRace();
    }
    fs.unlink(raceFile(id), () => broadcastHistory());
  });

  // Race control from the browser -> race timer. Only known commands with
  // sane arguments are forwarded; the timer validates again.
  socket.on('command', (msg) => {
    if (!msg || typeof msg.cmd !== 'string') return;
    let out;
    if (msg.cmd === 'start' || msg.cmd === 'reset' || msg.cmd === 'reload_racers') {
      out = { cmd: msg.cmd };
    } else if (msg.cmd === 'set_led_order' && Array.isArray(msg.order) &&
               msg.order.every(Number.isInteger)) {
      out = { cmd: msg.cmd, order: msg.order };
    } else if (msg.cmd === 'set_laps' && Number.isInteger(msg.laps) && msg.laps >= 1 && msg.laps <= 99) {
      out = { cmd: msg.cmd, laps: msg.laps };
    } else if (msg.cmd === 'test_led' && Number.isInteger(msg.led)) {
      out = { cmd: msg.cmd, led: msg.led };
    } else if (msg.cmd === 'set_checkpoints' && Array.isArray(msg.devices) && msg.devices.length >= 1 &&
               msg.devices.every((d) => typeof d === 'string' && DEVICE_RE.test(d))) {
      out = { cmd: msg.cmd, devices: msg.devices };
    } else if (msg.cmd === 'set_racers' && Array.isArray(msg.racers) && msg.racers.length <= 500) {
      // Tags unique hex, names one line of plain text (the timer writes them
      // to racers.csv as "tag,name").
      const racers = [];
      const seen = new Set();
      for (const r of msg.racers) {
        if (!r || typeof r.tag !== 'string' || typeof r.name !== 'string') return;
        const tag = r.tag.trim().toUpperCase();
        const name = r.name.replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, 40);
        if (!/^[0-9A-F]{1,20}$/.test(tag) || !name || seen.has(tag)) return;
        seen.add(tag);
        racers.push({ tag, name });
      }
      out = { cmd: msg.cmd, racers };
    } else if (msg.cmd === 'sim_tag' && Number.isInteger(msg.checkpoint) && msg.checkpoint >= 0 &&
               typeof msg.tag === 'string' && /^[0-9A-Fa-f]{1,20}$/.test(msg.tag)) {
      out = { cmd: msg.cmd, checkpoint: msg.checkpoint, tag: msg.tag.toUpperCase() };
    } else {
      return;
    }
    mqttClient.publish('race/command', JSON.stringify(out), { qos: 1 });
  });
});

server.listen(HTTP_PORT, () => {
  console.log(`Dashboard: http://localhost:${HTTP_PORT}`);
});
