# servo_ctrl.py — Controle do servo motor MG996R via PWM

from machine import Pin, PWM
import utime
import config


class Servo:
    """
    MG996R: 50 Hz, pulso entre 500 µs (0°) e 2500 µs (180°).
    A Pico usa duty_u16 (0–65535) para um período de 20 ms.
      duty = (pulse_us / 20000) * 65535
    """

    def __init__(self):
        self._pwm = PWM(Pin(config.SERVO_PIN))
        self._pwm.freq(config.SERVO_FREQ)
        self._angle = -1
        self.move(config.SERVO_CLOSE_DEG)

    def _angle_to_duty(self, degrees: int) -> int:
        degrees = max(0, min(180, degrees))
        pulse_us = config.SERVO_MIN_US + (config.SERVO_MAX_US - config.SERVO_MIN_US) * degrees // 180
        return int(pulse_us / 20000 * 65535)

    def move(self, degrees: int, hold_ms: int = 300):
        if degrees == self._angle:
            return
        self._pwm.duty_u16(self._angle_to_duty(degrees))
        self._angle = degrees
        utime.sleep_ms(hold_ms)

    def open(self):
        self.move(config.SERVO_OPEN_DEG, hold_ms=500)

    def close(self):
        self.move(config.SERVO_CLOSE_DEG, hold_ms=500)
        # corta PWM para não tremer quando parado
        self._pwm.duty_u16(0)

    @property
    def angle(self):
        return self._angle

    def deinit(self):
        self._pwm.deinit()
