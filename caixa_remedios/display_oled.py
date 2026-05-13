# display_oled.py — Gerenciador do display OLED SSD1306 128x32

from machine import I2C
import utime
import config

try:
    from lib.ssd1306 import SSD1306_I2C
except ImportError:
    from ssd1306 import SSD1306_I2C  # fallback se estiver na raiz


class OLEDDisplay:
    def __init__(self, i2c: I2C):
        self._oled = SSD1306_I2C(
            config.OLED_WIDTH, config.OLED_HEIGHT,
            i2c, addr=config.OLED_I2C_ADDR
        )
        self._active = True
        self._last_content = ""

    # ── controle de energia ───────────────────────────────────────────────────

    def on(self):
        if not self._active:
            self._oled.poweron()
            self._active = True

    def off(self):
        if self._active:
            self._oled.poweroff()
            self._active = False

    @property
    def is_on(self):
        return self._active

    # ── tela principal ────────────────────────────────────────────────────────

    def show_clock(self, time_str: str, next_alarm_str: str):
        """Linha 1: hora atual  |  Linha 2: próximo alarme."""
        content = time_str + next_alarm_str
        if content == self._last_content:
            return
        self._last_content = content
        self._oled.fill(0)
        self._oled.text(time_str, 20, 2, 1)
        self._oled.text(next_alarm_str, 0, 18, 1)
        self._oled.show()

    def show_alarm(self, med_name: str, time_str: str):
        """Tela de alarme: pisca nome do remédio."""
        self._oled.fill(0)
        self._oled.text("!! TOMAR !!", 12, 2, 1)
        # trunca nome se necessário (max 16 chars a 8px/char)
        name = med_name[:16]
        x = max(0, (128 - len(name) * 8) // 2)
        self._oled.text(name, x, 14, 1)
        self._oled.text(time_str, 32, 24, 1)
        self._oled.show()

    def show_message(self, line1: str, line2: str = "", line3: str = ""):
        self._oled.fill(0)
        self._oled.text(line1[:16], 0, 0, 1)
        if line2:
            self._oled.text(line2[:16], 0, 12, 1)
        if line3:
            self._oled.text(line3[:16], 0, 24, 1)
        self._oled.show()

    def show_menu(self, title: str, options: list, selected: int = -1):
        """Título + até 2 opções. Cabe em 128x32 (4 linhas × 8px)."""
        self._oled.fill(0)
        self._oled.text(title[:16], 0, 0, 1)
        self._oled.hline(0, 9, 128, 1)
        for i, opt in enumerate(options[:2]):
            y = 12 + i * 10
            prefix = ">" if i == selected else " "
            self._oled.text(f"{prefix}{opt}"[:16], 0, y, 1)
        self._oled.show()

    def show_med_grid(self, names: list):
        """
        Grade 2x2 — mostra os 4 remédios de uma vez em 128x32.
        names: lista de 4 strings.
        Layout:
          Selec.Remedio
          [1]Nnnnn [2]Nnnnn
          [3]Nnnnn [4]Nnnnn
        Cada célula = 8 chars × 8px = 64px de largura.
        """
        self._oled.fill(0)
        self._oled.text("Selec.Remedio", 0, 0, 1)
        for i in range(4):
            col = i % 2        # 0=esquerda, 1=direita
            row = i // 2       # 0=linha1, 1=linha2
            name = names[i][:5] if i < len(names) else "-----"
            cell = f"[{i+1}]{name}"   # ex: "[1]Aspir"
            x = col * 64
            y = 12 + row * 10
            self._oled.text(cell[:8], x, y, 1)
        self._oled.show()

    def show_input(self, prompt: str, value: str, cursor: bool = True):
        self._oled.fill(0)
        self._oled.text(prompt[:16], 0, 2, 1)
        display_val = value + ("_" if cursor else " ")
        self._oled.text(display_val[:16], 0, 18, 1)
        self._oled.show()

    def clear(self):
        self._oled.fill(0)
        self._oled.show()
        self._last_content = ""
