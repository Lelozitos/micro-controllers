# keypad.py — Wrapper do teclado matricial 4x4 usando lib do repositório EEN251
# Biblioteca base: lib/matrix_keyboard_4x4.py (Rodrigo França)

import config

try:
    from lib.matrix_keyboard_4x4 import MatrixKeyboard
except ImportError:
    from matrix_keyboard_4x4 import MatrixKeyboard


class Keypad:
    """
    Wrapper sobre MatrixKeyboard que expõe API scan() compatível com main.py.

    Wiring esperado (key_map do repo EEN251):
      rows_pins[0] → fileira inferior do teclado físico  (*, 0, #, D)
      rows_pins[3] → fileira superior do teclado físico  (1, 2, 3, A)
      cols_pins[0] → coluna direita (D, C, B, A)
      cols_pins[3] → coluna esquerda (*, 7, 4, 1)

    Teclas de navegação no menu:
      A = subir / incrementar    B = descer / decrementar
      # = confirmar              * = voltar / cancelar
      D = abrir menu
    """

    def __init__(self):
        self._kb = MatrixKeyboard(
            config.KEYPAD_ROWS,
            config.KEYPAD_COLS,
            debounce_time_ms=20
        )

    def scan(self) -> str | None:
        """
        Retorna a primeira tecla pressionada (string de 1 char) ou None.
        Não bloqueante — chame no loop principal.
        """
        keys = self._kb.get_pressed_keys()
        return keys[0] if keys else None

    def scan_all(self) -> list:
        """Retorna lista de todas as teclas pressionadas simultaneamente."""
        return self._kb.get_pressed_keys()

    def wait_key(self, timeout_ms: int = 30_000) -> str | None:
        """Bloqueia até uma tecla ser pressionada ou timeout esgotar."""
        import utime
        start = utime.ticks_ms()
        while utime.ticks_diff(utime.ticks_ms(), start) < timeout_ms:
            k = self.scan()
            if k:
                return k
            utime.sleep_ms(20)
        return None
