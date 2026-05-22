# servo_ctrl.py — SM-S4303R continuous rotation servo control
#
# SM-S4303R: continuous rotation servo, 50 Hz
#   ~1000 µs → CW spin (clockwise)    — open
#   ~1500 µs → stopped
#   ~2000 µs → CCW spin (counter-clockwise) — close
#
# Pico uses duty_u16 (0–65535). Conversion:
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
        """Spin CW for SERVO_OPEN_MS ms, then stop."""
        self._set(config.SERVO_CW_US)
        utime.sleep_ms(config.SERVO_OPEN_MS)
        self.stop()

    def close(self):
        """Spin CCW for SERVO_CLOSE_MS ms, then stop."""
        self._set(config.SERVO_CCW_US)
        utime.sleep_ms(config.SERVO_CLOSE_MS)
        self.stop()

    def run_cw(self, duration_ms: int):
        """Spin CW for duration_ms ms."""
        self._set(config.SERVO_CW_US)
        utime.sleep_ms(duration_ms)
        self.stop()

    def run_ccw(self, duration_ms: int):
        """Spin CCW for duration_ms ms."""
        self._set(config.SERVO_CCW_US)
        utime.sleep_ms(duration_ms)
        self.stop()

    def deinit(self):
        self.stop()
        self._pwm.deinit()
