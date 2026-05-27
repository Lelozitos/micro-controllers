# config.py — Pin assignments and global constants

# ─── I2C (DS1307 + OLED share the same bus) ──────────────────────────────────
I2C_ID       = 0    # I2C0
I2C_SDA_PIN  = 8    # GP8
I2C_SCL_PIN  = 9    # GP9
I2C_FREQ     = 400_000

RTC_I2C_ADDR  = 0x68
OLED_I2C_ADDR = 0x3C
OLED_WIDTH    = 128
OLED_HEIGHT   = 32

# ─── Servo HX5010 (positional) ───────────────────────────────────────────────
SERVO_PIN       = 5
SERVO_FREQ      = 50
SERVO_MIN_US    = 500
SERVO_MAX_US    = 2500
SERVO_OPEN_DEG  = 90
SERVO_CLOSE_DEG = 0

# ─── HC-SR501 Motion Sensor (desabilitado) ───────────────────────────────────
# PIR_PIN           = 6
# PIR_DEBOUNCE_MS   = 2000
# DISPLAY_TIMEOUT_S = 30

# ─── Buzzer (desabilitado) ────────────────────────────────────────────────────
# BUZZER_PIN  = 7
# BUZZER_FREQ = 2000
# BUZZER_DUTY = 32767

# ─── 4×4 Matrix Keypad ───────────────────────────────────────────────────────
KEYPAD_ROWS = [13, 12, 11, 10]
KEYPAD_COLS = [17, 16, 15, 14]

# ─── LEDs (one per medicine compartment) ─────────────────────────────────────
LED_PINS     = [18, 19, 20]
LED_BLINK_MS = 400

# ─── System ──────────────────────────────────────────────────────────────────
MAX_MEDS        = 3
MAX_ALARMS_EACH = 3
ALARM_FILE      = "alarms.json"
CONFIRM_TIMEOUT = 60
LOOP_SLEEP_MS   = 50

MED_NAMES = ["Remedio 1", "Remedio 2", "Remedio 3"]
