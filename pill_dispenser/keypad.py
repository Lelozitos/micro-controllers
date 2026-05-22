# keypad.py — 4×4 matrix keypad wrapper using the EEN251 repository lib
# Base library: lib/matrix_keyboard_4x4.py (Rodrigo França)

import config

try:
    from lib.matrix_keyboard_4x4 import MatrixKeyboard
except ImportError:
    from matrix_keyboard_4x4 import MatrixKeyboard


class Keypad:
    """
    Wrapper over MatrixKeyboard exposing a scan() API compatible with main.py.

    Expected wiring (EEN251 repo key_map):
      rows_pins[0] → bottom row of physical keypad  (*, 0, #, D)
      rows_pins[3] → top row of physical keypad  (1, 2, 3, A)
      cols_pins[0] → right column (D, C, B, A)
      cols_pins[3] → left column (*, 7, 4, 1)

    Menu navigation keys:
      A = up / increment    B = down / decrement
      # = confirm           * = back / cancel
      D = open menu
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
