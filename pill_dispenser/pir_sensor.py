# pir_sensor.py — HC-SR501 PIR sensor manager

from machine import Pin
import utime
import config


class PIRSensor:
    """
    Detects presence via HC-SR501 digital signal.
    Provides instantaneous state and "last detected" tracking
    for display timeout control.
    """

    def __init__(self):
        self._pin = Pin(config.PIR_PIN, Pin.IN)
        self._last_detected = utime.ticks_ms()
        self._debounce_ms = config.PIR_DEBOUNCE_MS

    def detected(self) -> bool:
        """True if sensor is currently active."""
        return self._pin.value() == 1

    def poll(self) -> bool:
        """
        Call in the main loop.
        Updates last-detected timestamp.
        Returns True if presence detected (with debounce).
        """
        if self._pin.value() == 1:
            now = utime.ticks_ms()
            if utime.ticks_diff(now, self._last_detected) > self._debounce_ms:
                self._last_detected = now
                return True
            self._last_detected = now  # renew even without returning True
        return False

    def seconds_since_last(self) -> int:
        """Seconds since last presence detection."""
        return utime.ticks_diff(utime.ticks_ms(), self._last_detected) // 1000

    def display_should_sleep(self) -> bool:
        return self.seconds_since_last() >= config.DISPLAY_TIMEOUT_S
