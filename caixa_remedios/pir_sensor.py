# pir_sensor.py — Gerenciador do sensor PIR HC-SR501

from machine import Pin
import utime
import config


class PIRSensor:
    """
    Detecta presença via sinal digital do HC-SR501.
    Fornece estado instantâneo e rastreamento de "última detecção"
    para controle de timeout do display.
    """

    def __init__(self):
        self._pin = Pin(config.PIR_PIN, Pin.IN)
        self._last_detected = utime.ticks_ms()
        self._debounce_ms = config.PIR_DEBOUNCE_MS

    def detected(self) -> bool:
        """True se sensor está ativo agora."""
        return self._pin.value() == 1

    def poll(self) -> bool:
        """
        Chame no loop principal.
        Atualiza timestamp de última detecção.
        Retorna True se houve detecção (com debounce).
        """
        if self._pin.value() == 1:
            now = utime.ticks_ms()
            if utime.ticks_diff(now, self._last_detected) > self._debounce_ms:
                self._last_detected = now
                return True
            self._last_detected = now  # renova mesmo sem retornar True
        return False

    def seconds_since_last(self) -> int:
        """Segundos desde a última presença detectada."""
        return utime.ticks_diff(utime.ticks_ms(), self._last_detected) // 1000

    def display_should_sleep(self) -> bool:
        return self.seconds_since_last() >= config.DISPLAY_TIMEOUT_S
