"""
@file matrix_keyboard_4x4.py
@brief Biblioteca para controlar um teclado de matriz 4x4 com Raspberry Pi Pico e MicroPython
@author Rodrigo França
@date 2023-03-17
Fonte: https://github.com/rodmarfran/EEN251_Sistemas_Embarcados

Mapa de teclas (wiring padrão do repositório):
  rows_pins[0] → fileira física inferior do teclado (*,0,#,D)
  rows_pins[3] → fileira física superior do teclado (1,2,3,A)
  cols_pins[0] → coluna física direita (D,C,B,A)
  cols_pins[3] → coluna física esquerda (*,7,4,1)
"""

from machine import Pin
import utime

class MatrixKeyboard:
    """Classe para manipular um teclado de matriz 4x4."""

    def __init__(self, rows_pins, cols_pins, debounce_time_ms=20):
        self.rows = [Pin(pin, Pin.OUT, value=1) for pin in rows_pins]
        self.cols = [Pin(pin, Pin.IN, Pin.PULL_UP) for pin in cols_pins]
        self.debounce_time_ms = debounce_time_ms
        self.last_keys = []
        self.key_map = [
            ['D', '#', '0', '*'],
            ['C', '9', '8', '7'],
            ['B', '6', '5', '4'],
            ['A', '3', '2', '1']
        ]

    def _set_row_mode(self, row, mode):
        """Define o modo do pino para uma linha."""
        row.init(mode=Pin.OUT if mode == 0 else Pin.IN, pull=None if mode == 1 else Pin.PULL_UP)

    def _scan_keys(self):
        """Escaneia o teclado para identificar quais teclas estão sendo pressionadas."""
        pressed_keys = []
        for row in self.rows:
            self._set_row_mode(row, 1)  # alta impedância

        for row_num, row in enumerate(self.rows):
            self._set_row_mode(row, 0)
            row.value(0)
            utime.sleep_ms(1)

            for col_num, col in enumerate(self.cols):
                if col.value() == 0:
                    pressed_keys.append((row_num, col_num))

            self._set_row_mode(row, 1)

        return pressed_keys

    def _debounce(self, keys):
        """Debouncing para estabilizar a detecção de teclas pressionadas."""
        if not keys:
            return keys
        utime.sleep_ms(self.debounce_time_ms)
        stable_keys = self._scan_keys()
        return [key for key in stable_keys if key in keys]

    def _scan_falling_edges(self, previous_keys):
        """Detecta bordas de descida nas teclas pressionadas."""
        current_keys = self._scan_keys()
        falling_edges = [key for key in current_keys if key not in previous_keys]
        return falling_edges

    def get_pressed_keys(self):
        """Obtém os caracteres das teclas que foram pressionadas após o debouncing."""
        falling_edges = self._scan_falling_edges(self.last_keys)
        key_chars = []
        if falling_edges:
            debounced_keys = self._debounce(falling_edges)
            if debounced_keys:
                key_chars = [self.key_map[row][col] for row, col in debounced_keys]
        self.last_keys = self._scan_keys()
        return key_chars
