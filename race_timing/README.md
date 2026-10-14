# Race Timing System (C++)

Raspberry Pi 4 + C++ — Finish-line timer for a single track that identifies **who** finished. An RFID reader is the finish line (no break-beam sensor), three red lights give the start countdown, red/green LEDs show whether a race can start or is running, and results go over MQTT to a live web dashboard (local Mosquitto by default, Ubidots optional).

Racers cross the finish line one at a time (running race, single lane); the system records finish order and split time per racer, not per lane.

---

## Raspberry Pi 4 GPIO Pinout

![Raspberry Pi 4 GPIO Pinout](https://www.raspberrypi.com/documentation/computers/images/GPIO.png)

---

## Components

| Component             | Model                                                             |
| ---------------------- | -------------------------------------------------------------------- |
| Single-board computer  | Raspberry Pi 4                                                       |
| Racer identification   | RDM6300 125 kHz RFID reader module, wired via USB-TTL adapter        |
| RFID tags              | 125 kHz EM4100 keyfob/card, one per racer                            |
| Start button           | Momentary pushbutton                                                 |
| Reset button           | Momentary pushbutton                                                 |
| Status LEDs            | Red (ready) + green (race running), 330 Ω resistors                  |
| Countdown lights       | 3 × red LED, 330 Ω resistors                                         |
| Buzzer                 | Active/passive buzzer                                                |
| Power supply           | 5 V 3 A USB-C adapter                                                |

---

## Why RFID: knowing *who* crossed

Each racer wears/carries a 125 kHz tag. The RDM6300 at the finish line streams the tag ID continuously while it's in range; the first read of a tag during a race is that racer's finish, and the name is looked up in `racers.csv`.

```
racer reaches the reader ──► tag read (~5-8 cm range)
                                   │
                                   ▼
          first read of this tag this race? ──► logged as next finisher
                                   │
                                   no ──► ignored (reader repeats every ~150 ms)
```

Tags not listed in `racers.csv` are still logged, as `UNKNOWN_TAG:<id>` — handy for finding out a new tag's ID (see [Registering racers](#registering-racers)).

---

## Wiring

### RFID reader (RDM6300)

The RDM6300 outputs plain UART (TTL) and does not need SPI/I2C — connect it through a USB-TTL adapter, exposed as `/dev/ttyUSB0`.

| USB-TTL adapter | RDM6300 pin | Notes                        |
| ----------------- | ------------ | ------------------------------ |
| 5V                | VCC          |                                 |
| GND               | GND          |                                 |
| RX                | TX           | reader only transmits, RX unused |

Mount the reader's coil right at the finish line; racers must bring their tag within a few cm of it.

### Start / Reset buttons

| Pi 4 pin (BCM) | Physical pin | Button        |
| --------------- | ------------ | -------------- |
| GPIO22          | 15           | Start (to GND) |
| GPIO23          | 16           | Reset (to GND) |

Internal pull-ups are enabled in software — wire the other leg straight to GND.

### Status LEDs, countdown lights & buzzer

| Pi 4 pin (BCM) | Physical pin | Part                                   |
| --------------- | ------------ | ---------------------------------------- |
| GPIO24          | 18           | Red LED — ready, race can start          |
| GPIO25          | 22           | Green LED — race in progress             |
| GPIO5           | 29           | Countdown red LED 1                      |
| GPIO6           | 31           | Countdown red LED 2                      |
| GPIO13          | 33           | Countdown red LED 3                      |
| GPIO18          | 12           | Buzzer +                                 |
| GND             | 14           | Buzzer −                                 |

Each LED: GPIO pin → 330 Ω resistor → LED long leg (anode); LED short leg (cathode) → GND.

| Light                | Meaning                                         |
| -------------------- | ------------------------------------------------ |
| Red (GPIO24)         | Ready — press START to begin a race              |
| Countdown 1 → 2 → 3  | Starting — one more light each second            |
| All countdown off    | GO — race clock starts                           |
| Green (GPIO25)       | Race in progress — finishes are being recorded   |

All pins are set in `config.hpp` (`READY_LED_PIN`, `RUNNING_LED_PIN`, `COUNTDOWN_LED_PINS`, `COUNTDOWN_STEP_MS`) — change them there and rebuild if you wire differently.

---

## Project Structure

```
race_timing/
├── CMakeLists.txt
├── config.hpp            — pin map, countdown timing, RFID port, MQTT settings
├── main.cpp               — button callbacks, countdown, race loop
├── gpio_io.hpp/.cpp       — ready/running LEDs, countdown lights, buzzer via pigpio
├── rfid_reader.hpp/.cpp   — background thread parsing RDM6300 UART frames, one callback per read
├── race_manager.hpp/.cpp  — race state machine, racer lookup, CSV log
├── mqtt_publisher.hpp/.cpp — Ubidots MQTT publish (libmosquitto)
├── racers.csv             — "tag_id,name" roster, one line per racer
├── race_log.csv           — created automatically, one row per finisher
└── dashboard/             — local web dashboard (Node.js), see below
    ├── package.json
    ├── server.js          — MQTT ⇄ Socket.IO bridge
    └── public/            — the dashboard page (HTML/CSS/JS)
```

---

## Building

```bash
sudo apt install cmake g++ libpigpio-dev libmosquitto-dev

cd race_timing
mkdir -p build && cd build
cmake ..
make

cd ..                     # run from the project folder so racers.csv is found
sudo ./build/race_timer   # GPIO access needs root
```

After changing any `.cpp`/`.hpp` file (e.g. pins in `config.hpp`), run `make` again inside `build/`. If you moved or copied the project folder, delete `build/` and redo the steps above — CMake remembers the old path.

---

## Running on the Pi (full setup)

Steps for a fresh Raspberry Pi 4 with the sensors wired up per the tables above.

**1. Base system**

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y git cmake g++ libpigpio-dev libmosquitto-dev mosquitto mosquitto-clients nodejs npm
sudo systemctl enable --now mosquitto
sudo systemctl disable --now pigpiod   # race_timer uses pigpio directly; the daemon would block it
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
- confirm `RFID_PORT` (`ls /dev/ttyUSB*` after plugging in the RDM6300's USB-TTL adapter to see what it enumerated as)

Edit `racers.csv` with the real racer tag IDs (see [Registering racers](#registering-racers)).

**4. Build**

```bash
mkdir -p build && cd build
cmake ..
make
```

**5. Run the timer** (from the project folder, not `build/`)

```bash
cd ~/race_timing
sudo ./build/race_timer   # sudo required for GPIO access
```

You should see `Race timer ready (red light). Press START to begin.` and the red LED comes on. Stop it with Ctrl+C (all LEDs turn off). Only one copy can run at a time.

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

Press START (physical button) — the 3 countdown lights come on one by one, then go out: green LED on, dashboard shows "RACE IN PROGRESS" with the clock running. Hold a racer's tag to the reader — the dashboard shows them in the leaderboard. RESET clears the race and the red light comes back.

---

## Registering racers

`racers.csv` has one `tag_id,name` line per racer:

```
0A00B1C2D3,Alice
0A00B1C2D4,Bob
```

To find a tag's ID: start the timer, run a race (START, wait for green), and hold the tag to the reader. The console prints `[#1] UNKNOWN_TAG:0A00B1C2D3 ...` — copy that 10-character ID into `racers.csv`. The file is read when the timer starts, so restart it after editing.

---

## How It Works

### Race cycle

```
1. [START] button pressed (red "ready" LED goes off)
        │
        ▼
2. Countdown: red lights 1, 2, 3 come on one per second (beep each)
        │   (RESET during the countdown aborts back to ready)
        ▼
3. All three go out → GO: race state → running, green LED on, long beep
        │
        ▼
4. A racer's tag is read at the finish line (first read this race only)
        │
        ├── tag resolved to a racer name via racers.csv
        ├── elapsed = now - race_start recorded, finish order incremented
        ├── short beep, printed to console
        └── published via MQTT with racer name + place
        │
        ▼
   (repeat step 4 for every racer)
        │
        ▼
5. [RESET] button pressed
        │
        ├── all finishes appended to race_log.csv
        └── red "ready" LED on, green off, state → idle
```

---

## Bring-up checklist

Once the code builds, work through these in order on the real hardware:

1. **Stop anything holding the GPIO**: `sudo pkill race_timer; sudo systemctl disable --now pigpiod`.
2. **Wire the LEDs** per [Status LEDs, countdown lights & buzzer](#status-leds-countdown-lights--buzzer) and start the timer (`sudo ./build/race_timer` from the project folder) — the red ready LED should light.
3. **Test the buttons**: START → countdown 1, 2, 3 → all off → green. RESET → red again. If nothing happens, see [Troubleshooting](#troubleshooting).
4. **Plug in the RFID reader** and check it appears: `ls /dev/ttyUSB*`. Restart the timer — the `no RFID reader` warning must be gone.
5. **Register the tags** in `racers.csv` (see [Registering racers](#registering-racers)) and restart the timer.
6. **Full run with the dashboard open** at `http://<Pi's IP>:3000`: START → countdown → GO → hold a tag to the reader → racer appears on the leaderboard → RESET.
7. Optionally delete old test entries from `race_log.csv`, and set up [auto-start on boot](#start-automatically-on-boot-systemd).

---

## Start automatically on boot (systemd)

Instead of typing the commands after every reboot, run the timer and the dashboard as systemd services. The paths below assume the project is at `/home/pi/race_timing` — change every path if yours differs (e.g. `/home/pi/Desktop/race_timing`).

**1. Stop any copy you started by hand** (only one timer can hold the GPIO):

```bash
sudo pkill race_timer
pkill -f "node server.js"
sudo systemctl disable --now pigpiod
```

**2. Create the timer service:**

```bash
sudo nano /etc/systemd/system/race-timer.service
```

```ini
[Unit]
Description=Race timer (GPIO + RFID + MQTT)
After=mosquitto.service
Wants=mosquitto.service
Conflicts=pigpiod.service

[Service]
WorkingDirectory=/home/pi/race_timing
# stdbuf -oL: flush each console line so it shows up in journalctl right away
ExecStart=/usr/bin/stdbuf -oL /home/pi/race_timing/build/race_timer
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

It runs as root (needed for GPIO). `WorkingDirectory` must be the project folder so `racers.csv` and `race_log.csv` are found.

**3. Create the dashboard service:**

```bash
sudo nano /etc/systemd/system/race-dashboard.service
```

```ini
[Unit]
Description=Race timing web dashboard
After=network-online.target mosquitto.service
Wants=network-online.target mosquitto.service

[Service]
User=pi
WorkingDirectory=/home/pi/race_timing/dashboard
Environment=MQTT_HOST=localhost
ExecStart=/usr/bin/node server.js
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Run `npm install` in `dashboard/` once beforehand.

**4. Enable and start both:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now race-timer race-dashboard
```

**5. Check they're running:**

```bash
systemctl status race-timer race-dashboard
journalctl -u race-timer -f      # live console output: countdown, finishers (Ctrl+C to stop watching)
```

Reboot (`sudo reboot`) to confirm everything comes back on its own: red LED on, dashboard reachable at `http://<Pi's IP>:3000`.

**Day-to-day:**

| Task | Command |
| ---- | ------- |
| Restart after editing `racers.csv` | `sudo systemctl restart race-timer` |
| After changing code | `cd build && make && sudo systemctl restart race-timer` |
| Run by hand again (e.g. debugging) | `sudo systemctl stop race-timer`, then `sudo ./build/race_timer` |
| Turn auto-start off | `sudo systemctl disable --now race-timer race-dashboard` |

While the service is running, starting `race_timer` by hand fails with `Can't lock /var/run/pigpio.pid` — stop the service first.

---

## Telemetry: MQTT

By default (`config.hpp`) the timer publishes to the local Mosquitto broker on the Pi (`MQTT_HOST = "localhost"`, `MQTT_TOKEN = ""`), which is what the local dashboard reads. Every event goes to two plain topics:

| Topic          | Payload                                                    | When                    |
| -------------- | ----------------------------------------------------------- | ----------------------- |
| `race/state`   | `{"state":"running","started_at":<epoch_ms>}` or `{"state":"idle"}` | GO (end of countdown) / RESET / timer start-up |
| `race/finish`  | `{"place":1,"racer":"Alice","elapsed_s":12.4}`             | each racer's first tag read during a race |

Watch them live with `mosquitto_sub -h localhost -t 'race/#' -v`.

### Optional: Ubidots cloud

`mqtt_publisher.cpp` mirrors the auth pattern from the EEN251 course slides (`EEN251 - HTTP e MQTT - Raspberry Pi e Node-RED.pdf`): the Ubidots **API token** as the MQTT username, blank password, broker `industrial.api.ubidots.com:1883`. Each publish carries the split time plus the racer's name and finish order as Ubidots "context":

```json
{"finish_time": {"value": 12.4, "context": {"racer": "Alice", "place": 1}}}
```

To use it, set `MQTT_HOST = "industrial.api.ubidots.com"` and `MQTT_TOKEN` to your Ubidots **Default token** (Profile → API Credentials) in `config.hpp`, then rebuild. Note the local dashboard then needs `MQTT_HOST=industrial.api.ubidots.com MQTT_TOKEN=<token> npm start` to see the events — with the placeholder token nothing reaches either side.

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

## Troubleshooting

| Symptom | Cause / fix |
| ------- | ----------- |
| `initInitialise: Can't lock /var/run/pigpio.pid` | Another program already holds the GPIO — a second `race_timer`, the `race-timer` service, or the `pigpiod` daemon. Run `sudo systemctl stop race-timer; sudo pkill race_timer; sudo systemctl disable --now pigpiod` and start again. |
| `warning: no RFID reader on /dev/ttyUSB0` | The USB-TTL adapter isn't plugged in, or got another name — check `ls /dev/ttyUSB* /dev/ttyACM*` and update `RFID_PORT`. Races still start/reset, but no finishes are recorded. |
| Dashboard never changes | Timer and dashboard aren't on the same broker. Check `MQTT_HOST` in `config.hpp` (then rebuild) and the `MQTT_HOST` given to `npm start`; `mosquitto_sub -h localhost -t 'race/#' -v` shows whether events arrive. |
| Button does nothing | While the timer runs, `watch -n 0.2 pigs r 22` (START) / `pigs r 23` (RESET) should go `1` → `0` when pressed. If it stays `1`, check the wiring: pin 15/16 to one leg, GND to the other — on 4-leg buttons use diagonally opposite legs. |
| Racer shows as `UNKNOWN_TAG:<id>` | Tag not in `racers.csv` (or the timer wasn't restarted after editing it, or it was started from inside `build/`). |
| Racer finishes only once | By design — repeat reads of the same tag in one race are ignored. Press RESET to start over. |

The buttons use pigpio *alert* callbacks rather than `gpioSetISRFunc`: ISRs go through sysfs GPIO, which recent Raspberry Pi kernels number from 512, so they silently fail to register.

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
