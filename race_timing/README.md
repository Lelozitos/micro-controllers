# Race Timing System (C++)

Raspberry Pi 4 + C++ — Finish-line timer for a single track that identifies **who** crossed, using one break-beam sensor + one RFID reader, with MQTT telemetry (Ubidots) and a Node-RED dashboard.

Racers cross the finish line one at a time (running race, single lane); the system records finish order and split time per racer, not per lane.

---

## Raspberry Pi 4 GPIO Pinout

![Raspberry Pi 4 GPIO Pinout](https://www.raspberrypi.com/documentation/computers/images/GPIO.png)

---

## Components

| Component             | Model                                                             |
| ---------------------- | -------------------------------------------------------------------- |
| Single-board computer  | Raspberry Pi 4                                                       |
| Finish-line trigger    | IR break-beam pair (emitter + receiver)                              |
| Racer identification   | RDM6300 125 kHz RFID reader module, wired via USB-TTL adapter        |
| RFID tags              | 125 kHz EM4100 keyfob/card, one per racer                            |
| Start button           | Momentary pushbutton                                                 |
| Reset button           | Momentary pushbutton                                                 |
| Status LED             | Green (idle/ready) + 330 Ω resistor                                  |
| Alert LED              | Red (race running) + 330 Ω resistor                                  |
| Buzzer                 | Active/passive buzzer                                                |
| Power supply           | 5 V 3 A USB-C adapter                                                |

---

## Why RFID: knowing *who* crossed

A break-beam sensor alone only tells you *someone* crossed — it can't tell racers apart or match a name to a time. The RDM6300 reader sits right next to the beam: each racer wears/carries a 125 kHz tag, and the reader streams the tag ID continuously over UART while it's in range. When the beam breaks, the program takes whatever tag was seen most recently (within `TAG_MATCH_WINDOW_S`, default 3 s) and looks up the racer's name in `racers.csv`.

```
racer approaches ──► RFID tag read (continuous, ~5-8 cm range)
                             │
                             ▼
racer crosses beam ──► finish-line interrupt fires
                             │
                             ▼
        last-seen tag (within 3 s) + elapsed time ──► logged as next finisher
```

If no tag was read in time, the crossing is still logged as `UNKNOWN` rather than silently dropped.

---

## Wiring

### Finish-line break-beam sensor

| Pi 4 pin (BCM) | Physical pin | Sensor pin              |
| --------------- | ------------ | ------------------------- |
| GPIO17          | 11           | Receiver OUT              |
| 5V              | 2 / 4        | Emitter + Receiver VCC    |
| GND             | 6 / 9        | Emitter + Receiver GND    |

### RFID reader (RDM6300)

The RDM6300 outputs plain UART (TTL) and does not need SPI/I2C — connect it through a USB-TTL adapter, exposed as `/dev/ttyUSB0`.

| USB-TTL adapter | RDM6300 pin | Notes                        |
| ----------------- | ------------ | ------------------------------ |
| 5V                | VCC          |                                 |
| GND               | GND          |                                 |
| RX                | TX           | reader only transmits, RX unused |

Mount the reader's coil directly at the beam so a tag is only in range right as the beam breaks.

### Start / Reset buttons

| Pi 4 pin (BCM) | Physical pin | Button        |
| --------------- | ------------ | -------------- |
| GPIO22          | 15           | Start (to GND) |
| GPIO23          | 16           | Reset (to GND) |

Internal pull-ups are enabled in software — wire the other leg straight to GND.

### Status LEDs & buzzer

| Pi 4 pin (BCM) | Physical pin | Part                |
| --------------- | ------------ | --------------------- |
| GPIO24          | 18           | Green LED — idle/ready |
| GPIO25          | 22           | Red LED — race running |
| GPIO18          | 12           | Buzzer +               |
| GND             | 14           | Buzzer −               |

Each LED needs a 330 Ω series resistor.

---

## Project Structure

```
race_timing/
├── CMakeLists.txt
├── config.hpp            — pin map, RFID port, MQTT/Ubidots credentials
├── main.cpp               — pigpio ISR wiring, race loop
├── gpio_io.hpp/.cpp       — status LEDs + buzzer via pigpio
├── rfid_reader.hpp/.cpp   — background thread parsing RDM6300 UART frames
├── race_manager.hpp/.cpp  — race state machine, racer lookup, CSV log
├── mqtt_publisher.hpp/.cpp — Ubidots MQTT publish (libmosquitto)
├── racers.csv             — "tag_id,name" roster, one line per racer
├── race_log.csv           — created automatically, one row per crossing
└── dashboard/             — local web dashboard (Node.js), see below
    ├── package.json
    ├── server.js          — MQTT ⇄ Socket.IO bridge
    └── public/            — the dashboard page (HTML/CSS/JS)
```

---

## Building

```bash
sudo apt install cmake g++ libpigpio-dev libmosquitto-dev
sudo apt install pigpio  # pigpio daemon/library, if not already present

cd race_timing
mkdir build && cd build
cmake ..
make

sudo ./race_timer   # GPIO access needs root (or run pigpiod and use gpioInitialise remotely)
```

Edit `racers.csv` with each racer's tag ID (read it once with any RDM6300 to find the ID) and name before running.

---

## Running on the Pi (full setup)

Steps for a fresh Raspberry Pi 4 with the sensors wired up per the tables above.

**1. Base system**

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y git cmake g++ libpigpio-dev libmosquitto-dev mosquitto mosquitto-clients nodejs npm
sudo systemctl enable --now mosquitto
sudo systemctl enable --now pigpiod
```

**2. Get the code**

```bash
git clone <your-repo> ~/race_timing
cd ~/race_timing
```

**3. Configure**

Edit `config.hpp`:
- `MQTT_HOST` = `"localhost"` (Mosquitto running on the Pi itself)
- `MQTT_TOKEN` = `""` (empty — local Mosquitto has no auth by default)
- confirm `BEAM_PIN` and `RFID_PORT` (`ls /dev/ttyUSB*` after plugging in the RDM6300's USB-TTL adapter to see what it enumerated as)

Edit `racers.csv` with the real racer tag IDs.

**4. Build**

```bash
mkdir build && cd build
cmake ..
make
```

**5. Run the timer**

```bash
sudo ./race_timer   # sudo required for GPIO access
```

**6. Run the dashboard** (on the Pi itself, or any other machine on the same network)

```bash
cd ~/race_timing/dashboard
npm install
MQTT_HOST=<Pi's IP> npm start
```

If running on the Pi itself, `MQTT_HOST=localhost`. Find the Pi's IP with `hostname -I`.

**7. Open the dashboard**

From any device on the same network: `http://<Pi's IP>:3000`

**8. Test it**

Press START (physical button) — red LED on, buzzer beeps, dashboard shows "RACE IN PROGRESS" with the clock running. Walk a tagged racer through the beam — the dashboard shows them in the leaderboard. RESET clears the race.

---

## How It Works

### Race cycle

```
1. [START] button pressed
        │
        ▼
2. Race state → running, red LED on, buzzer beeps (300 ms)
        │
        ▼
3. Each racer's RFID tag is read continuously as they approach the line
        │
        ▼
4. Beam breaks → finish-line ISR fires
        │
        ├── last tag seen (≤ 3 s old) resolved to a racer name
        ├── elapsed = now - race_start recorded, finish order incremented
        ├── short beep, printed to console
        └── published to Ubidots via MQTT with racer name + place in context
        │
        ▼
   (repeat step 3-4 for every racer crossing the line)
        │
        ▼
5. [RESET] button pressed
        │
        ├── all crossings appended to race_log.csv
        └── green LED on, state → idle
```

A crossing sooner than `MIN_CROSSING_GAP_S` (default 0.3 s) after the previous one is ignored — this only filters sensor bounce, not real racers, since a running race can have finishers seconds apart.

---

## Telemetry: MQTT to Ubidots

`mqtt_publisher.cpp` mirrors the auth pattern from the EEN251 course slides (`EEN251 - HTTP e MQTT - Raspberry Pi e Node-RED.pdf`): the Ubidots **API token** as the MQTT username, blank password, broker `industrial.api.ubidots.com:1883`. Each publish carries the split time plus the racer's name and finish order as Ubidots "context":

```json
{"finish_time": {"value": 12.4, "context": {"racer": "Alice", "place": 1}}}
```

Set `MQTT_TOKEN` in `config.hpp` to your Ubidots **Default token** (Profile → API Credentials).

Alongside the Ubidots-shaped topic, every event is also published to two plain topics meant for the local dashboard below:

| Topic          | Payload                                                    | When                    |
| -------------- | ----------------------------------------------------------- | ----------------------- |
| `race/state`   | `{"state":"running","started_at":<epoch_ms>}` or `{"state":"idle"}` | START / RESET pressed   |
| `race/finish`  | `{"place":1,"racer":"Alice","elapsed_s":12.4}`             | every finish-line crossing |

---

## Local Web Dashboard

A self-hosted, no-cloud-required dashboard: a small Node.js server bridges MQTT to the browser over Socket.IO and serves a live-updating page — race state banner, running stopwatch, and an animated leaderboard as racers finish.

```
race_timer (C++, on the Pi) ──MQTT──► broker ──MQTT──► dashboard/server.js ──Socket.IO──► browser
```

**1. Run an MQTT broker somewhere both the Pi and your computer can reach** — a local Mosquitto instance is the simplest, fully offline option:

```bash
sudo apt install mosquitto        # on the Pi, or on your own PC
sudo systemctl enable --now mosquitto
```

Point `MQTT_HOST` in `config.hpp` at that broker's address (`localhost` if the timer and broker run on the same machine, otherwise the broker's LAN IP), rebuild, and leave `MQTT_TOKEN` empty if your Mosquitto has no auth configured. Using Ubidots' cloud broker instead works too — the dashboard only cares about the `race/state`/`race/finish` topics either way.

**2. Run the dashboard** (on your computer, the Pi, or any machine on the same network as the broker):

```bash
cd race_timing/dashboard
npm install
MQTT_HOST=<broker address> npm start
```

**3. Open `http://localhost:3000`** (or `http://<that machine's IP>:3000` from another device on the network, e.g. a phone or a screen at the finish line).

The page shows READY / RACE IN PROGRESS, a live clock synced to the race's `started_at`, and a leaderboard that fills in — with position, racer name, and split time — as each `race/finish` message arrives. A late-opened browser still gets the current state and standings (the server replays them on connect).

---

## Building the Node-RED Dashboard (alternative)

Same node types as the reference slides' Ubidots examples:

1. **`mqtt in`** subscribed to `/v1.6/devices/race_timer`, authenticated with the Ubidots token.
2. **`json`** node to parse the payload.
3. **`function`** node pulling out `finish_time.value`, `finish_time.context.racer` and `.place`.
4. **dashboard `text`** widget appending each finisher as `"#{{place}} {{racer}} — {{time}}s"`.
5. **dashboard `chart`** node plotting finish times, and a `template` widget for the live results list.

```
mqtt in ──► json ──► function (extract time + racer + place) ──┬──► text: latest finisher
                                                                  ├──► template: results table
                                                                  └──► chart: finish-time history
```

---

## External Libraries

| Library      | Source                                                           |
| ------------- | ------------------------------------------------------------------ |
| `pigpio`      | [abyz.me.uk/rpi/pigpio](https://abyz.me.uk/rpi/pigpio/)            |
| `libmosquitto`| [mosquitto.org](https://mosquitto.org/)                            |
| `express`, `socket.io`, `mqtt` (Node.js, dashboard only) | npm |

---

## References

- `EEN251 - HTTP e MQTT - Raspberry Pi e Node-RED.pdf` (course material) — MQTT auth model, Ubidots dashboards, Node-RED nodes.
- [Ubidots hardware API docs](https://ubidots.com/docs/hw/)
- [RDM6300 datasheet / frame format](https://www.sunrom.com/get/224900)
