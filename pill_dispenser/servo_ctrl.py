from machine import Pin, PWM
import config


def _angle_to_duty(deg: float) -> int:
    pulse_us = config.SERVO_MIN_US + (deg / 180) * (config.SERVO_MAX_US - config.SERVO_MIN_US)
    return int(pulse_us / 20000 * 65535)


class Servo:
    def __init__(self):
        self._pwm = PWM(Pin(config.SERVO_PIN), freq=config.SERVO_FREQ)
        self.write_angle(config.SERVO_CLOSE_DEG)

    def write_angle(self, deg: float):
        deg = max(0, min(180, deg))
        self._pwm.duty_u16(_angle_to_duty(deg))

    def open(self):
        self.write_angle(config.SERVO_OPEN_DEG)

    def close(self):
        self.write_angle(config.SERVO_CLOSE_DEG)

    def deinit(self):
        self._pwm.deinit()
