# config.py — Pinagem e constantes globais do sistema
# Pinos alinhados com o padrão do repositório EEN251_Sistemas_Embarcados
# Repo: https://github.com/rodmarfran/EEN251_Sistemas_Embarcados

# ─── I2C (DS1307 + OLED compartilham o mesmo barramento) ─────────────────────
# Padrão do curso: SDA=GP8, SCL=GP9
I2C_ID       = 0    # I2C0
I2C_SDA_PIN  = 8    # GP8  ← padrão EEN251
I2C_SCL_PIN  = 9    # GP9  ← padrão EEN251
I2C_FREQ     = 400_000

RTC_I2C_ADDR  = 0x68
EEPROM_I2C_ADDR = 0x50  # AT24C32N do módulo Tiny RTC
OLED_I2C_ADDR = 0x3C
OLED_WIDTH    = 128
OLED_HEIGHT   = 32

# ─── Servo MG996R ─────────────────────────────────────────────────────────────
SERVO_PIN      = 2   # GP2 (PWM)
SERVO_FREQ     = 50
SERVO_MIN_US   = 500
SERVO_MAX_US   = 2500
SERVO_OPEN_DEG  = 90
SERVO_CLOSE_DEG = 0

# ─── Motor DC (L298N) ─────────────────────────────────────────────────────────
MOTOR_IN1_PIN  = 3   # GP3
MOTOR_IN2_PIN  = 4   # GP4
MOTOR_ENA_PIN  = 5   # GP5 (PWM)
MOTOR_FREQ     = 1000
MOTOR_SPEED    = 60000
MOTOR_RUN_MS   = 800

# ─── Sensor de Presença HC-SR501 ──────────────────────────────────────────────
PIR_PIN            = 6   # GP6
PIR_DEBOUNCE_MS    = 2000
DISPLAY_TIMEOUT_S  = 30

# ─── Buzzer ───────────────────────────────────────────────────────────────────
BUZZER_PIN    = 7    # GP7 (PWM)
BUZZER_FREQ   = 2000
BUZZER_DUTY   = 32767

# ─── Teclado Matricial 4x4 ────────────────────────────────────────────────────
# Linhas: GP10–GP13  |  Colunas: GP14–GP17
# (separadas do I2C em GP8/GP9 para evitar conflito)
#
# Wiring físico (seguindo o key_map do repositório EEN251):
#   rows_pins[0]=GP10 → fileira INFERIOR do teclado  (*,0,#,D)
#   rows_pins[1]=GP11 → fileira (7,8,9,C)
#   rows_pins[2]=GP12 → fileira (4,5,6,B)
#   rows_pins[3]=GP13 → fileira SUPERIOR do teclado  (1,2,3,A)
#   cols_pins[0]=GP14 → coluna DIREITA  (D,C,B,A)
#   cols_pins[1]=GP15 → coluna (#,9,6,3)
#   cols_pins[2]=GP16 → coluna (0,8,5,2)
#   cols_pins[3]=GP17 → coluna ESQUERDA (*,7,4,1)
KEYPAD_ROWS = [10, 11, 12, 13]  # GP10–GP13
KEYPAD_COLS = [14, 15, 16, 17]  # GP14–GP17

# ─── LEDs (um por compartimento de remédio) ───────────────────────────────────
LED_PINS = [18, 19, 20, 21]  # GP18–GP21 → compartimentos 1–4
LED_BLINK_MS = 400

# ─── Sistema ──────────────────────────────────────────────────────────────────
MAX_MEDS        = 4
MAX_ALARMS_EACH = 4
ALARM_FILE      = "alarms.json"
CONFIRM_TIMEOUT = 60
LOOP_SLEEP_MS   = 50

MED_NAMES = ["Remedio 1", "Remedio 2", "Remedio 3", "Remedio 4"]
