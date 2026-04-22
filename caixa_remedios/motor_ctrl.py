# motor_ctrl.py — Controle do Motor DC via L298N

from machine import Pin, PWM
import utime
import config


class DCMotor:
    """
    Controla um motor DC via ponte H L298N.
      IN1, IN2: direção
      ENA: velocidade (PWM)
    Uso típico: avançar esteira para o compartimento correto.
    """

    def __init__(self):
        self._in1 = Pin(config.MOTOR_IN1_PIN, Pin.OUT)
        self._in2 = Pin(config.MOTOR_IN2_PIN, Pin.OUT)
        self._ena  = PWM(Pin(config.MOTOR_ENA_PIN))
        self._ena.freq(config.MOTOR_FREQ)
        self._stop()

    def _stop(self):
        self._in1.value(0)
        self._in2.value(0)
        self._ena.duty_u16(0)

    def _forward(self, speed: int = None):
        speed = speed or config.MOTOR_SPEED
        self._in1.value(1)
        self._in2.value(0)
        self._ena.duty_u16(speed)

    def _backward(self, speed: int = None):
        speed = speed or config.MOTOR_SPEED
        self._in1.value(0)
        self._in2.value(1)
        self._ena.duty_u16(speed)

    def advance_slot(self, slots: int = 1):
        """Avança N compartimentos na esteira."""
        for _ in range(slots):
            self._forward()
            utime.sleep_ms(config.MOTOR_RUN_MS)
            self._stop()
            utime.sleep_ms(200)

    def home(self):
        """Retorna ao compartimento inicial (reverso por tempo fixo)."""
        self._backward()
        utime.sleep_ms(config.MOTOR_RUN_MS * config.MAX_MEDS)
        self._stop()

    def stop(self):
        self._stop()

    def deinit(self):
        self._stop()
        self._ena.deinit()
