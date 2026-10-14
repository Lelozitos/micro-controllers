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
| Racer identification   | RC522 (MFRC522) 13.56 MHz RFID reader, wired to the Pi's SPI pins    |
| RFID tags              | 13.56 MHz MIFARE card/keyfob (the ones sold with the RC522), one per racer |
| Start button           | Momentary pushbutton                                                 |
| Reset button           | Momentary pushbutton                                                 |
| Status LEDs            | Red (ready) + green (race running), 330 Ω resistors                  |
| Start lights (F1)      | 3 × red LED, 330 Ω resistors                                         |
| Power supply           | 5 V 3 A USB-C adapter                                                |

---

## Why RFID: knowing *who* crossed

Each racer carries a 13.56 MHz card or keyfob. The Pi asks the RC522 at the line for a card every 50 ms; when a racer holds their tag to the reader, its ID (UID) is read and looked up in `racers.csv`. **Every pass of the line is a lap.**

```
racer reaches the reader ──► tag read (~3 cm range)
                                   │
                                   ▼
     same tag read < 3 s ago? ──yes──► ignored (still the same pass — the
                                   │              reader repeats every 50 ms)
                                   no
                                   ▼
                     lap counted (lap time + total time)
                                   │
                                   ▼
        reached the set number of laps? ──yes──► WINNER, chequered flag
        or flag already out?                     (others: classified on
                                                  their next pass)
```

The 3 s gap is `MIN_LAP_S` in `config.hpp`; it also stops a tag held at the line when the lights go out from counting as a lap.

Tags not listed in `racers.csv` are still logged, as `UNKNOWN_TAG:<id>` — handy for finding out a new tag's ID (see [Registering racers](#registering-racers)).

---

## Wiring

### RFID reader (RC522)

The RC522 connects straight to the Pi's pins over SPI — no adapter. It runs on **3.3 V only**: never connect its 3.3V pin to 5V.

| RC522 pin  | Pi physical pin | Pi GPIO (BCM)   |
| ---------- | --------------- | --------------- |
| SDA (SS)   | 24              | GPIO8 (SPI CE0) |
| SCK        | 23              | GPIO11 (SCLK)   |
| MOSI       | 19              | GPIO10 (MOSI)   |
| MISO       | 21              | GPIO9 (MISO)    |
| IRQ        | — not connected | —               |
| GND        | 20 (any GND)    | —               |
| RST        | 17 or 1 (3.3 V) | — (keeps the reader on) |
| 3.3V       | 17 (or 1)       | 3.3 V           |

SPI must be enabled once (it's off by default):

```bash
sudo raspi-config nonint do_spi 0     # or: sudo raspi-config → Interface Options → SPI → Yes
ls /dev/spidev0.0                     # must exist
```

Mount the reader right at the finish line; racers must hold their tag within ~3 cm of it.

### Start / Reset buttons

| Pi 4 pin (BCM) | Physical pin | Button        |
| --------------- | ------------ | -------------- |
| GPIO22          | 15           | Start (to GND) |
| GPIO23          | 16           | Reset (to GND) |

Internal pull-ups are enabled in software — wire the other leg straight to GND.

### Status LEDs & countdown lights

| Pi 4 pin (BCM) | Physical pin | Part                                   |
| --------------- | ------------ | ---------------------------------------- |
| GPIO24          | 18           | Red LED — ready, race can start          |
| GPIO25          | 22           | Green LED — race in progress             |
| GPIO5           | 29           | Start light A (red)                      |
| GPIO6           | 31           | Start light B (red)                      |
| GPIO13          | 33           | Start light C (red)                      |

**Finding the pins:** the header is easy to miscount — count from the end *away* from the SD card. The inner row (odd numbers) ends with pin 39 (GND), and going back: 37, 35, **33 (light C)**, **31 (light B)**, **29 (light A)** — so the start lights are the 3rd, 4th and 5th inner-row pins from the end. Not sure? Stop the timer and run `./led_test.sh`: it lights GPIO24, 25, 5, 6 and 13 one at a time for 3 s each and prints which one is on (`./led_test.sh 17 27` tests any GPIO numbers you give).

Each LED: GPIO pin → 330 Ω resistor → LED long leg (anode); LED short leg (cathode) → GND.

| Light                | Meaning                                         |
| -------------------- | ------------------------------------------------ |
| Red (GPIO24)         | Ready — press START to begin a race              |
| Start lights on 1 → 2 → 3 | Starting — one more light each second (order set from the dashboard) |
| All lit, then all off at once | Lights out = GO — race clock starts. The hold with all lit is random (1–3 s), like F1, so nobody can anticipate it |
| Green (GPIO25)       | Race in progress — laps are being recorded; blinks off briefly each time a lap is counted. Stays on after the chequered flag until RESET |

All pins and timings are set in `config.hpp` (`READY_LED_PIN`, `RUNNING_LED_PIN`, `COUNTDOWN_LED_PINS`, `COUNTDOWN_STEP_MS`, `COUNTDOWN_HOLD_MIN_MS`/`MAX_MS`) — change them there and rebuild if you wire differently. The order the start lights come on in is changed from the dashboard (see [Local Web Dashboard](#local-web-dashboard)), not in the code; it's saved in `race_settings.txt` together with the number of laps.

---

## Project Structure

```
race_timing/
├── CMakeLists.txt
├── config.hpp            — pin map, countdown timing, RFID port, MQTT settings
├── main.cpp               — buttons + dashboard commands, F1 start lights, race loop
├── gpio_io.hpp/.cpp       — ready/running LEDs, countdown lights, RC522 reset pin via pigpio
├── rfid_reader.hpp/.cpp   — RC522 driver over SPI (spidev), polls for cards in a background thread
├── race_manager.hpp/.cpp  — race state machine, racer lookup, CSV log
├── mqtt_publisher.hpp/.cpp — MQTT link: race events out, dashboard commands in (libmosquitto)
├── led_test.sh            — lights each LED pin in turn, to check the wiring
├── racers.csv             — "tag_id,name" roster, one line per racer
├── race_log.csv           — created automatically, one row per lap (saved on RESET)
├── race_settings.txt      — created automatically: laps + start light order set from the dashboard
└── dashboard/             — local web dashboard (Node.js), see below
    ├── package.json
    ├── server.js          — MQTT ⇄ Socket.IO bridge, forwards START/RESET/light-order commands
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

# LEDs off from power-on until the timer starts (GPIO5/6 default to pull-up and would glow)
echo 'gpio=5,6,13,24,25=op,dl' | sudo tee -a /boot/firmware/config.txt
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
- enable SPI for the RC522: `sudo raspi-config nonint do_spi 0`, then check `ls /dev/spidev0.0`

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
A1B2C3D4,Alice
E5F60718,Bob
```

To find a tag's ID: start the timer (no race needed) and hold the tag flat on the reader. The console prints `Tag on reader: A1B2C3D4  (UNKNOWN_TAG:A1B2C3D4)` — copy that 8-character ID into `racers.csv` with the racer's name, save, and restart the timer (the file is read at start-up). Hold the tag again: it now shows `Tag on reader: A1B2C3D4  (Alice)`.

---

## How It Works

### Race cycle

```
1. START pressed — physical button or dashboard (red "ready" LED goes off)
        │
        ▼
2. Start lights come on one by one, one per second, in the saved order
        │   (RESET during the lights aborts back to ready)
        ▼
3. All lit for a random 1–3 s, then all go out at once
        │
        ▼
   Lights out = GO: race state → running, green LED on
        │
        ▼
4. A racer passes the reader → lap counted (ignored if same tag < 3 s ago)
        │
        ├── tag resolved to a racer name via racers.csv
        ├── lap number, lap time and total time recorded
        ├── green LED blinks, printed to console, sent to the dashboard
        │
        ▼
5. First racer to complete the set laps → WINNER, chequered flag
        │
        ▼
6. Every other racer is classified (2nd, 3rd, …) on their next pass,
   with however many laps they had — like F1
        │
        ▼
7. RESET pressed — physical button or dashboard
        │
        ├── every lap appended to race_log.csv
        └── red "ready" LED on, green off, state → idle
```

---

## Bring-up checklist

Once the code builds, work through these in order on the real hardware:

1. **Stop anything holding the GPIO**: `sudo pkill race_timer; sudo systemctl disable --now pigpiod`.
2. **Wire the LEDs** per [Status LEDs & countdown lights](#status-leds--countdown-lights) and check them with `./led_test.sh` (timer stopped): red, green, then start lights A, B, C should each light for 3 s, in that order. Then start the timer (`sudo ./build/race_timer` from the project folder) — the red ready LED should light.
3. **Test the buttons**: START → countdown 1, 2, 3 → all off → green. RESET → red again. If nothing happens, see [Troubleshooting](#troubleshooting).
4. **Wire the RC522 and enable SPI** per [RFID reader (RC522)](#rfid-reader-rc522). Restart the timer — it must print `RC522 reader ready`, not a warning.
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

By default (`config.hpp`) the timer publishes to the local Mosquitto broker on the Pi (`MQTT_HOST = "localhost"`, `MQTT_TOKEN = ""`), which is what the local dashboard reads. Topics:

| Topic          | Direction | Payload                                                    | When                    |
| -------------- | --------- | ----------------------------------------------------------- | ----------------------- |
| `race/state`   | timer → | `{"state":"countdown","lit":N}`, `{"state":"running","started_at":<epoch_ms>,"laps":N}`, `{"state":"finished","winner":"Alice","elapsed_s":160.4,"laps":N}` or `{"state":"idle"}` | each start light / lights out / chequered flag / RESET / timer start-up |
| `race/lap`     | timer → | `{"tag":"A1B2C3D4","racer":"Alice","lap":2,"lap_time_s":31.2,"elapsed_s":63.0}` | every counted lap |
| `race/finish`  | timer → | `{"place":1,"tag":"A1B2C3D4","racer":"Alice","laps":5,"elapsed_s":160.4}` | a racer is classified (winner, then the others on their next pass) |
| `race/config`  | timer → (retained) | `{"led_order":[2,0,1],"led_pins":[5,6,13],"laps":5}` | timer start-up / settings changed |
| `race/timer`   | timer → (retained) | `online` / `offline` (MQTT last will)                | timer connects / stops or crashes |
| `race/command` | → timer | `{"cmd":"start"}`, `{"cmd":"reset"}`, `{"cmd":"set_led_order","order":[2,0,1]}`, `{"cmd":"test_led","led":0}`, `{"cmd":"set_laps","laps":5}` | sent by the dashboard |

`led_order` lists start-light indices (0 = A on GPIO5, 1 = B on GPIO6, 2 = C on GPIO13) in the order they come on. You can also control the timer without the dashboard, e.g. `mosquitto_pub -h localhost -t race/command -m '{"cmd":"start"}'`.

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

Point `MQTT_HOST` in `config.hpp` at that broker's address (`localhost` if the timer and broker run on the same machine, otherwise the broker's LAN IP), rebuild, and leave `MQTT_TOKEN` empty if your Mosquitto has no auth configured. Using Ubidots' cloud broker instead works too — the dashboard uses the same `race/...` topics either way.

**2. Run the dashboard** (on your computer, the Pi, or any machine on the same network as the broker):

```bash
cd race_timing/dashboard
npm install
MQTT_HOST=<broker address> npm start
```

**3. Open `http://localhost:3000`** (or `http://<that machine's IP>:3000` from another device on the network, e.g. a phone or a screen at the finish line).

The page shows:

- **Status card** — READY / STARTING / RACE IN PROGRESS, with the three start lights mirrored live and a race clock synced to the race's `started_at`.
- **START / RESET buttons** — same as the physical buttons. START only works while the timer is ready (red); RESET works any time, including during the start lights. If the race timer isn't running, both are greyed out and a warning shows.
- **Laps** — how many laps the race is (1–99), with −/+ or by typing. Only changeable while the timer is ready; saved on the Pi.
- **Status card** lap counter — `LAP 2 / 5` for the leader; CHEQUERED FLAG with the winner's name and time once they finish.
- **Leaderboard** — live standings: position, racer, laps done, last lap time and total time. Classified racers (🏁) come first in finishing order, then everyone else by most laps. A late-opened browser still gets the current state and standings (the server replays them on connect).
- **Start lights order** — the order the three lights come on in. Use ▲/▼ to move a light earlier or later, **Test** to flash that physical light for 1 s (to find out which LED is which), then **Save order**. Only editable while the timer is ready. The order is saved on the Pi (`race_settings.txt`) and kept across restarts.

Anyone who can open the page on your network can start and reset races — keep it on a private network.

---

## Troubleshooting

| Symptom | Cause / fix |
| ------- | ----------- |
| `initInitialise: Can't lock /var/run/pigpio.pid` | Another program already holds the GPIO — a second `race_timer`, the `race-timer` service, or the `pigpiod` daemon. Run `sudo systemctl stop race-timer; sudo pkill race_timer; sudo systemctl disable --now pigpiod` and start again. |
| `open /dev/spidev0.0: No such file or directory` | SPI is disabled — `sudo raspi-config nonint do_spi 0` (reboot if it still doesn't appear). |
| `RC522 not detected ... (version reg = 0x00)` or `0xFF` | Wiring: check SDA→24, SCK→23, MOSI→19, MISO→21, RST→3.3V (pin 1 or 17), 3.3V→17 and GND. Loose jumper wires are the usual cause. |
| Dashboard buttons greyed out, "Race timer is offline" | `race_timer` isn't running or isn't connected to the same broker. Start it, and check `MQTT_HOST` matches on both sides. |
| LEDs stay on with nothing running | The timer turns every LED off when it stops (Ctrl+C, closed terminal, `kill`, `systemctl stop`) — but not after `kill -9` or a power cut. Faint glow on start lights A/B after a reboot: add `gpio=5,6,13,24,25=op,dl` to `/boot/firmware/config.txt` (see setup step 1). |
| An LED never lights | Stop the timer and run `./led_test.sh`. If an LED stays dark on its pin, move its wire to pin 22 and run `./led_test.sh 25`: if it lights there, the wire was on the wrong header pin (count again from the end, see [Finding the pins](#status-leds--countdown-lights)); if not, check the LED's direction, resistor and GND. |
| `RC522 reader ready` but tags never register | Hold the tag flat against the reader for a second; only 13.56 MHz (MIFARE) tags work, 125 kHz ones don't. A race must be running (green). |
| Dashboard never changes | Timer and dashboard aren't on the same broker. Check `MQTT_HOST` in `config.hpp` (then rebuild) and the `MQTT_HOST` given to `npm start`; `mosquitto_sub -h localhost -t 'race/#' -v` shows whether events arrive. |
| Button does nothing | While the timer runs, `watch -n 0.2 pigs r 22` (START) / `pigs r 23` (RESET) should go `1` → `0` when pressed. If it stays `1`, check the wiring: pin 15/16 to one leg, GND to the other — on 4-leg buttons use diagonally opposite legs. |
| Racer shows as `UNKNOWN_TAG:<id>` | Tag not in `racers.csv` (or the timer wasn't restarted after editing it, or it was started from inside `build/`). |
| A pass counts twice / a lap is missed | Reads of the same tag closer together than `MIN_LAP_S` (3 s) are one pass. Raise it if racers linger at the reader; lower it if a real lap can be shorter than 3 s. |
| A racer's passes stop counting | They've been classified (🏁) — after the chequered flag each racer finishes on their next pass. Press RESET for a new race. |

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
- [NXP MFRC522 datasheet](https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf)
