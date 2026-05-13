# servo_ctrl.py — Controle do servo de rotação contínua SM-S4303R
#
# SM-S4303R: servo de rotação contínua, 50 Hz
#   ~1000 µs → gira CW  (sentido horário)    — abre
#   ~1500 µs → parado
#   ~2000 µs → gira CCW (anti-horário)        — fecha
#
# Pico usa duty_u16 (0–65535). Conversão:
#   duty = pulse_us / 20000 * 65535

from machine import Pin, PWM
import utime
import config


def _us_to_duty(pulse_us: int) -> int:
    return int(pulse_us / 20000 * 65535)


class Servo:
    def __init__(self):
        self._pwm = PWM(Pin(config.SERVO_PIN), freq=config.SERVO_FREQ)
        self.stop()

    def _set(self, pulse_us: int):
        self._pwm.duty_u16(_us_to_duty(pulse_us))

    def stop(self):
        self._set(config.SERVO_STOP_US)

    def open(self):
        """Gira CW por SERVO_OPEN_MS ms, depois para."""
        self._set(config.SERVO_CW_US)
        utime.sleep_ms(config.SERVO_OPEN_MS)
        self.stop()

    def close(self):
        """Gira CCW por SERVO_CLOSE_MS ms, depois para."""
        self._set(config.SERVO_CCW_US)
        utime.sleep_ms(config.SERVO_CLOSE_MS)
        self.stop()

    def run_cw(self, duration_ms: int):
        """Gira CW por duration_ms ms."""
        self._set(config.SERVO_CW_US)
        utime.sleep_ms(duration_ms)
        self.stop()

    def run_ccw(self, duration_ms: int):
        """Gira CCW por duration_ms ms."""
        self._set(config.SERVO_CCW_US)
        utime.sleep_ms(duration_ms)
        self.stop()

    def deinit(self):
        self.stop()
        self._pwm.deinit()
