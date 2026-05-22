# Smart Medicine Box

Raspberry Pi Pico + MicroPython — Medication alarm system with servo, OLED display, keypad and PIR motion sensor.

---

## Raspberry Pi Pico Pinout

![Raspberry Pi Pico Pinout](https://embarcados.com.br/wp-content/uploads/2023/07/image-8.png)

---

## Components

| Component       | Model                            |
| --------------- | -------------------------------- |
| Microcontroller | Raspberry Pi Pico                |
| Display         | OLED SSD1306 128×32 I2C          |
| Clock           | RTC DS1307 (Tiny RTC I2C module) |
| Keypad          | 4×4 Matrix                       |
| Servo           | SM-S4303R (continuous rotation)  |
| Motion sensor   | HC-SR501 (PIR)                   |
| LEDs            | 4× colored LED + 330 Ω resistor  |
| Buzzer          | Passive buzzer                   |
| Power supply    | 5 V 2 A adapter                  |

---

## Wiring

### I2C — shared bus (OLED + DS1307)

| Pico pin | GPIO | Device        | Device pin |
| -------- | ---- | ------------- | ---------- |
| GP8      | SDA  | OLED SSD1306  | SDA        |
| GP8      | SDA  | DS1307        | SDA        |
| GP9      | SCL  | OLED SSD1306  | SCL        |
| GP9      | SCL  | DS1307        | SCL        |
| 3V3      | —    | OLED + DS1307 | VCC        |
| GND      | —    | OLED + DS1307 | GND        |

> The **Tiny RTC I2C** module includes DS1307 (address `0x68`) and AT24C32N EEPROM (`0x50`).  
> Insert a **CR2032 battery** in the module to keep the time when the Pico is powered off.

---

### Servo SM-S4303R

| Pico pin | GPIO | Servo wire             |
| -------- | ---- | ---------------------- |
| GP2      | PWM  | Signal (orange/yellow) |
| VSYS     | 5 V  | VCC (red)              |
| GND      | GND  | GND (brown/black)      |

The SM-S4303R is a **continuous rotation servo** — no angular position, it spins as long as the PWM pulse deviates from 1500 µs:

| Pulse    | Behaviour        |
| -------- | ---------------- |
| ~1000 µs | CW spin (open)   |
| ~1500 µs | stopped          |
| ~2000 µs | CCW spin (close) |

The code spins for a fixed duration (`SERVO_OPEN_MS` / `SERVO_CLOSE_MS` in `config.py`) then sends the stop pulse.

> **Calibration:** if the servo drifts (spins slowly at rest), adjust `SERVO_STOP_US` in `config.py` by ±50 µs until it stops completely.  
> Adjust `SERVO_OPEN_MS` and `SERVO_CLOSE_MS` to match the mechanical travel of your mechanism.

---

### HC-SR501 Motion Sensor

| Pico pin | GPIO | HC-SR501 pin |
| -------- | ---- | ------------ |
| GP6      | IN   | OUT          |
| 3V3      | —    | VCC          |
| GND      | —    | GND          |

---

### Passive Buzzer

| Pico pin | GPIO | Buzzer       |
| -------- | ---- | ------------ |
| GP7      | PWM  | + (positive) |
| GND      | —    | − (negative) |

---

### 4×4 Matrix Keypad

Key map and wiring order:

```
┌─────┬─────┬─────┬─────┐
│  1  │  2  │  3  │  A  │  ← top row     → GP13
├─────┼─────┼─────┼─────┤
│  4  │  5  │  6  │  B  │               → GP12
├─────┼─────┼─────┼─────┤
│  7  │  8  │  9  │  C  │               → GP11
├─────┼─────┼─────┼─────┤
│  *  │  0  │  #  │  D  │  ← bottom row → GP10
└─────┴─────┴─────┴─────┘
   ↑     ↑     ↑     ↑
  GP17  GP16  GP15  GP14
```

| Keypad row             | Pico GPIO | Keypad column        | Pico GPIO |
| ---------------------- | --------- | -------------------- | --------- |
| R4 — bottom (\*,0,#,D) | GP10      | C4 — right (A,B,C,D) | GP14      |
| R3 — (7,8,9,C)         | GP11      | C3 — (#,9,6,3)       | GP15      |
| R2 — (4,5,6,B)         | GP12      | C2 — (0,8,5,2)       | GP16      |
| R1 — top (1,2,3,A)     | GP13      | C1 — left (1,4,7,\*) | GP17      |

---

### Indicator LEDs

One LED per medicine compartment. Each LED requires a **330 Ω resistor** in series.

| Compartment | Pico GPIO | Suggested colour |
| ----------- | --------- | ---------------- |
| Medicine 1  | GP18      | Green            |
| Medicine 2  | GP19      | Blue             |
| Medicine 3  | GP20      | Yellow           |
| Medicine 4  | GP21      | Red              |

---

### Power

```
5 V 2 A adapter
    │
    ├──► VSYS (Pico pin 39) — powers Pico and servo
    └──► GND  (any Pico GND)
```

Pico internally regulates 3.3 V for OLED, DS1307, PIR and keypad.

---

## Project Structure

```
caixa_remedios/
├── main.py             — main loop + state machine
├── config.py           — pin assignments and constants (edit here)
├── alarm_manager.py    — alarm CRUD + JSON persistence
├── menu.py             — interactive menu via keypad + OLED
├── rtc_ds1307.py       — DS1307 wrapper
├── display_oled.py     — pre-formatted screens for SSD1306 128×32
├── keypad.py           — MatrixKeyboard wrapper
├── servo_ctrl.py       — PWM control for SM-S4303R (continuous rotation)
├── pir_sensor.py       — PIR sensor with presence timeout
├── led_ctrl.py         — non-blocking blink via ticks_ms
├── buzzer.py           — beep patterns (ok, error, alarm)
└── lib/
    ├── ds1307.py              — DS1307 driver (Mike Causer)
    ├── matrix_keyboard_4x4.py — keypad driver (Rodrigo França / EEN251)
    ├── at24c32n.py            — AT24C32N EEPROM driver
    └── ssd1306.py             — SSD1306 OLED driver
```

---

## Uploading via Thonny

1. Install **Thonny ≥ 4.0** and connect the Pico via USB.
2. Go to **Run → Configure interpreter → MicroPython (Raspberry Pi Pico)**.
3. Open the file panel: **View → Files**.
4. In the left panel (computer), navigate to the `caixa_remedios/` folder.
5. Select **all files** and the `lib/` folder. Right-click → **Upload to /**.
6. Expected structure on the Pico:
   ```
   /main.py
   /config.py
   /alarm_manager.py
   /menu.py
   /rtc_ds1307.py
   /display_oled.py
   /keypad.py
   /servo_ctrl.py
   /pir_sensor.py
   /led_ctrl.py
   /buzzer.py
   /lib/ds1307.py
   /lib/matrix_keyboard_4x4.py
   /lib/at24c32n.py
   /lib/ssd1306.py
   ```
7. Press **F5** with `main.py` open to run. The Pico runs `main.py` automatically on power-up.

---

## How It Works

### Main screen (normal mode)

```
  08:30:15
Next:09:00 Aspirn
```

Line 1: current time. Line 2: next alarm + medicine name.

Display turns off automatically after **30 seconds** with no motion detected by the PIR.

---

### Alarm cycle

```
1. Alarm time reached
        │
        ▼
2. Compartment LED blinks
   Buzzer plays 3 beeps
   Servo spins CW for SERVO_OPEN_MS ms (opens)
   Display shows "!! TAKE IT !!"
        │
        ├── User presses [#] ──► Dose confirmed
        │                         Servo spins CCW for SERVO_CLOSE_MS ms (closes)
        │                         LED turns off
        │                         Buzzer plays confirmation melody
        │
        └── No response in 60 s ──► Servo closes, system resumes
                                    (unconfirmed dose logged to serial)
```

---

### Configuration menu

Press **[D]** at any time to open the menu.

```
Navigation keys:
  [A] = up / increment
  [B] = down / decrement
  [#] = confirm / select
  [*] = back / cancel
  [D] = open menu
```

```
MENU
  1 – Medicines
        ├─ Select [1]–[4] (all visible in 2×2 grid)
        │     ├─ 1 – Add alarm     → type HH:MM + [#]
        │     ├─ 2 – View alarms   → [#] deletes, [*] back
        │     └─ 3 – Enable/Disable
        │
  2 – Set time   → type HH:MM:SS + [#]
  3 – Set date   → type DD/MM/YY + [#]
```

---

## External Libraries

All included in `lib/` — no extra installation needed.

| Library                  | Source                                                                                 |
| ------------------------ | -------------------------------------------------------------------------------------- |
| `ssd1306.py`             | [MicroPython official](https://github.com/micropython/micropython-lib)                 |
| `ds1307.py`              | [mcauser/micropython-tinyrtc-i2c](https://github.com/mcauser/micropython-tinyrtc-i2c)  |
| `matrix_keyboard_4x4.py` | [EEN251_Sistemas_Embarcados](https://github.com/rodmarfran/EEN251_Sistemas_Embarcados) |
| `at24c32n.py`            | [mcauser/micropython-tinyrtc-i2c](https://github.com/mcauser/micropython-tinyrtc-i2c)  |
