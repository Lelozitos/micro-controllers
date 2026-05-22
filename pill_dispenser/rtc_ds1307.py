# rtc_ds1307.py — DS1307 wrapper using the EEN251 repository lib
# Base library: lib/ds1307.py (Mike Causer / Rodrigo França)
#
# Lib base tuple: (year, month, day, weekday, hour, minute, second, 0)
#                   [0]    [1]    [2]   [3]     [4]   [5]     [6]   [7]

from machine import I2C, Pin
import config

try:
    from lib.ds1307 import DS1307 as _DS1307
except ImportError:
    from ds1307 import DS1307 as _DS1307


class RTC:
    """
    Wrapper over lib/ds1307.py exposing a convenient API for the project.
    Internal _DS1307 object accessible via .raw if needed.
    """

    def __init__(self, i2c: I2C):
        self.raw = _DS1307(i2c, addr=config.RTC_I2C_ADDR)
        # Ensure oscillator is running
        self.raw.halt(False)

    # ── read ──────────────────────────────────────────────────────────────────

    def datetime(self) -> tuple:
        """
        Returns (year, month, day, weekday, hour, minute, second, 0).
        Same format as the base lib — use indices [4]=hour, [5]=min, [6]=sec.
        """
        return self.raw.datetime()

    def now(self) -> tuple:
        """Returns (hour, minute, second) — shortcut for the main loop."""
        dt = self.raw.datetime()
        return dt[4], dt[5], dt[6]

    def time_str(self) -> str:
        h, m, s = self.now()
        return f"{h:02d}:{m:02d}:{s:02d}"

    def date_str(self) -> str:
        dt = self.raw.datetime()
        return f"{dt[2]:02d}/{dt[1]:02d}/{dt[0]}"

    # ── write ─────────────────────────────────────────────────────────────────

    def set_datetime(self, year, month, day, hour, minute, second, weekday=1):
        """Set full date and time."""
        self.raw.datetime((year, month, day, weekday, hour, minute, second, 0))

    def set_time(self, hour, minute, second=0):
        """Update only hour/minute/second, keeping current date."""
        dt = self.raw.datetime()
        self.raw.datetime((dt[0], dt[1], dt[2], dt[3], hour, minute, second, 0))

    def set_date(self, day, month, year):
        """Update only date, keeping current time."""
        dt = self.raw.datetime()
        self.raw.datetime((year, month, day, dt[3], dt[4], dt[5], dt[6], 0))
