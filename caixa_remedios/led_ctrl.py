# led_ctrl.py — Controle de LEDs indicadores (um por compartimento)

from machine import Pin
import utime
import config


class LEDController:
    """
    Gerencia até MAX_MEDS LEDs.
    Suporta: acender, apagar, piscar não bloqueante.
    O método update() deve ser chamado no loop principal.
    """

    def __init__(self):
        self._leds   = [Pin(p, Pin.OUT, value=0) for p in config.LED_PINS]
        self._blink  = [False] * config.MAX_MEDS   # quais estão piscando
        self._state  = [False] * config.MAX_MEDS   # estado atual do pino
        self._last_t = utime.ticks_ms()

    def on(self, index: int):
        if 0 <= index < config.MAX_MEDS:
            self._blink[index] = False
            self._leds[index].value(1)
            self._state[index] = True

    def off(self, index: int):
        if 0 <= index < config.MAX_MEDS:
            self._blink[index] = False
            self._leds[index].value(0)
            self._state[index] = False

    def blink(self, index: int):
        """Habilita piscar para o LED index."""
        if 0 <= index < config.MAX_MEDS:
            self._blink[index] = True

    def all_off(self):
        for i in range(config.MAX_MEDS):
            self.off(i)

    def update(self):
        """Chame no loop principal — alterna LEDs em modo blink."""
        now = utime.ticks_ms()
        if utime.ticks_diff(now, self._last_t) < config.LED_BLINK_MS:
            return
        self._last_t = now
        for i in range(config.MAX_MEDS):
            if self._blink[i]:
                self._state[i] = not self._state[i]
                self._leds[i].value(1 if self._state[i] else 0)
