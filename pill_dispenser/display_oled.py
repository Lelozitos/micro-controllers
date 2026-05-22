# display_oled.py — SSD1306 128×32 OLED display manager

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

    # ── power control ─────────────────────────────────────────────────────────

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

    # ── main screens ──────────────────────────────────────────────────────────

    def show_clock(self, time_str: str, next_alarm_str: str):
        """Line 1: current time  |  Line 2: next alarm."""
        content = time_str + next_alarm_str
        if content == self._last_content:
            return
        self._last_content = content
        self._oled.fill(0)
        self._oled.text(time_str, 20, 2, 1)
        self._oled.text(next_alarm_str, 0, 18, 1)
        self._oled.show()

    def show_alarm(self, med_name: str, time_str: str):
        """Alarm screen: shows medicine name."""
        self._oled.fill(0)
        self._oled.text("!! TAKE IT !!", 8, 2, 1)
        # truncate name if needed (max 16 chars at 8px/char)
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
        """Title + up to 2 options. Fits 128×32 (4 lines × 8px)."""
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
        2×2 grid — shows all 4 medicines at once on 128×32.
        names: list of 4 strings.
        Layout:
          Sel.Medicine
          [1]Nnnnn [2]Nnnnn
          [3]Nnnnn [4]Nnnnn
        Each cell = 8 chars × 8px = 64px wide.
        """
        self._oled.fill(0)
        self._oled.text("Sel.Medicine", 0, 0, 1)
        for i in range(4):
            col = i % 2        # 0=left, 1=right
            row = i // 2       # 0=row1, 1=row2
            name = names[i][:5] if i < len(names) else "-----"
            cell = f"[{i+1}]{name}"   # ex: "[1]Aspir"
            x = col * 64
            y = 12 + row * 10
            self._oled.text(cell[:8], x, y, 1)
        self._oled.show()

    def show_info(self, time_str: str, alarms: list):
        """Info screen: current time + up to 2 upcoming alarms."""
        content = time_str + str(alarms)
        if content == self._last_content:
            return
        self._last_content = content
        self._oled.fill(0)
        x = max(0, (128 - len(time_str) * 8) // 2)
        self._oled.text(time_str[:16], x, 0, 1)
        for i, a in enumerate(alarms[:2]):
            self._oled.text(a[:16], 0, 13 + i * 11, 1)
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
