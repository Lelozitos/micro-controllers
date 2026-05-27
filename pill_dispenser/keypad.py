# keypad.py — 4×4 matrix keypad wrapper
# Base library: lib/matrix_keyboard_4x4.py (Rodrigo França)

import config

try:
    from lib.matrix_keyboard_4x4 import MatrixKeyboard
except ImportError:
    from matrix_keyboard_4x4 import MatrixKeyboard


class Keypad:
    """
    Wrapper over MatrixKeyboard exposing a scan() API compatible with main.py.

    Wiring (KEYPAD_ROWS reversed top→bottom):
      rows_pins[0] → top row    (1, 2, 3, A)
      rows_pins[3] → bottom row (*, 0, #, D)
      cols_pins[0] → right column (D, C, B, A)
      cols_pins[3] → left column  (*, 7, 4, 1)

    Menu navigation:
      [A] up   [B] down   [#] confirm   [*] back   [D] open menu
    """

    def __init__(self):
        self._kb = MatrixKeyboard(
            config.KEYPAD_ROWS,
            config.KEYPAD_COLS,
            debounce_time_ms=20
        )

    def scan(self) -> str | None:
        """
        Returns the first pressed key (1-char string) or None.
        Non-blocking — call in the main loop.
        """
        keys = self._kb.get_pressed_keys()
        return keys[0] if keys else None

    def scan_all(self) -> list:
        """Returns list of all simultaneously pressed keys."""
        return self._kb.get_pressed_keys()

    def wait_key(self, timeout_ms: int = 30_000) -> str | None:
        """Blocks until a key is pressed or timeout expires."""
        import utime
        start = utime.ticks_ms()
        while utime.ticks_diff(utime.ticks_ms(), start) < timeout_ms:
            k = self.scan()
            if k:
                return k
            utime.sleep_ms(20)
        return None
