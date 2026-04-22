# rtc_ds1307.py — Wrapper do DS1307 usando a lib do repositório EEN251
# Biblioteca base: lib/ds1307.py (Mike Causer / Rodrigo França)
#
# Tuple da lib base: (year, month, day, weekday, hour, minute, second, 0)
#                      [0]    [1]    [2]   [3]     [4]   [5]     [6]   [7]

from machine import I2C, Pin
import config

try:
    from lib.ds1307 import DS1307 as _DS1307
except ImportError:
    from ds1307 import DS1307 as _DS1307


class RTC:
    """
    Wrapper sobre lib/ds1307.py que expõe API conveniente para o projeto.
    Mantém o objeto _DS1307 interno acessível via .raw se necessário.
    """

    def __init__(self, i2c: I2C):
        self.raw = _DS1307(i2c, addr=config.RTC_I2C_ADDR)
        # Garante oscilador ligado
        self.raw.halt(False)

    # ── leitura ───────────────────────────────────────────────────────────────

    def datetime(self) -> tuple:
        """
        Retorna (year, month, day, weekday, hour, minute, second, 0).
        Mesmo formato da lib base — use índices [4]=hora, [5]=min, [6]=seg.
        """
        return self.raw.datetime()

    def now(self) -> tuple:
        """Retorna (hour, minute, second) — atalho para o loop principal."""
        dt = self.raw.datetime()
        return dt[4], dt[5], dt[6]

    def time_str(self) -> str:
        h, m, s = self.now()
        return f"{h:02d}:{m:02d}:{s:02d}"

    def date_str(self) -> str:
        dt = self.raw.datetime()
        return f"{dt[2]:02d}/{dt[1]:02d}/{dt[0]}"

    # ── escrita ───────────────────────────────────────────────────────────────

    def set_datetime(self, year, month, day, hour, minute, second, weekday=1):
        """Define data/hora completa."""
        self.raw.datetime((year, month, day, weekday, hour, minute, second, 0))

    def set_time(self, hour, minute, second=0):
        """Atualiza apenas hora/minuto/segundo, mantendo data atual."""
        dt = self.raw.datetime()
        self.raw.datetime((dt[0], dt[1], dt[2], dt[3], hour, minute, second, 0))

    def set_date(self, day, month, year):
        """Atualiza apenas data, mantendo hora atual."""
        dt = self.raw.datetime()
        self.raw.datetime((year, month, day, dt[3], dt[4], dt[5], dt[6], 0))
