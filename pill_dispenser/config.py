# config.py — Pin assignments and global constants

# ─── I2C (DS1307 + OLED share the same bus) ──────────────────────────────────
I2C_ID       = 0    # I2C0
I2C_SDA_PIN  = 8    # GP8
I2C_SCL_PIN  = 9    # GP9
I2C_FREQ     = 400_000

RTC_I2C_ADDR  = 0x68
EEPROM_I2C_ADDR = 0x50  # AT24C32N do módulo Tiny RTC
OLED_I2C_ADDR = 0x3C
OLED_WIDTH    = 128
OLED_HEIGHT   = 32

# ─── Servo SM-S4303R (continuous rotation) ───────────────────────────────────
# 50 Hz pulse: ~1000 µs = max CW  |  ~1500 µs = stopped  |  ~2000 µs = max CCW
SERVO_PIN      = 2      # GP2 (PWM)
SERVO_FREQ     = 50
SERVO_STOP_US  = 1500   # stopped — tune if servo drifts at rest
SERVO_CW_US    = 1000   # open (clockwise)
SERVO_CCW_US   = 2000   # close (counter-clockwise)
SERVO_OPEN_MS  = 600    # duration (ms) to open — adjust for your mechanism
SERVO_CLOSE_MS = 600    # duration (ms) to close

# ─── HC-SR501 Motion Sensor ───────────────────────────────────────────────────
PIR_PIN            = 6   # GP6
PIR_DEBOUNCE_MS    = 2000
DISPLAY_TIMEOUT_S  = 30

# ─── Buzzer ──────────────────────────────────────────────────────────────────
BUZZER_PIN    = 7    # GP7 (PWM)
BUZZER_FREQ   = 2000
BUZZER_DUTY   = 32767

# ─── 4×4 Matrix Keypad ───────────────────────────────────────────────────────
# Rows: GP10–GP13  |  Columns: GP14–GP17
#   rows_pins[0]=GP10 → BOTTOM row (*,0,#,D)
#   rows_pins[1]=GP11 → row (7,8,9,C)
#   rows_pins[2]=GP12 → row (4,5,6,B)
#   rows_pins[3]=GP13 → TOP row (1,2,3,A)
#   cols_pins[0]=GP14 → RIGHT column (D,C,B,A)
#   cols_pins[1]=GP15 → column (#,9,6,3)
#   cols_pins[2]=GP16 → column (0,8,5,2)
#   cols_pins[3]=GP17 → LEFT column (*,7,4,1)
KEYPAD_ROWS = [10, 11, 12, 13]  # GP10–GP13
KEYPAD_COLS = [14, 15, 16, 17]  # GP14–GP17

# ─── LEDs (one per medicine compartment) ─────────────────────────────────────
LED_PINS = [18, 19, 20, 21]  # GP18–GP21 → compartimentos 1–4
LED_BLINK_MS = 400

# ─── System ──────────────────────────────────────────────────────────────────
MAX_MEDS        = 4
MAX_ALARMS_EACH = 4
ALARM_FILE      = "alarms.json"
CONFIRM_TIMEOUT = 60
LOOP_SLEEP_MS   = 50

MED_NAMES = ["Medicine 1", "Medicine 2", "Medicine 3", "Medicine 4"]
