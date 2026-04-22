# buzzer.py — Controle de buzzer piezoelétrico via PWM

from machine import Pin, PWM
import utime
import config


class Buzzer:
    """
    Buzzer passivo controlado por PWM.
    Padrões de beep pré-definidos para diferentes eventos.
    """

    def __init__(self):
        self._pwm = PWM(Pin(config.BUZZER_PIN))
        self._pwm.freq(config.BUZZER_FREQ)
        self._pwm.duty_u16(0)
        self._active    = False
        self._pattern   = []   # lista de (freq, duration_ms, pause_ms)
        self._step      = 0
        self._step_time = 0

    # ── helpers internos ──────────────────────────────────────────────────────

    def _beep(self, freq: int, duration_ms: int):
        self._pwm.freq(freq)
        self._pwm.duty_u16(config.BUZZER_DUTY)
        utime.sleep_ms(duration_ms)
        self._pwm.duty_u16(0)

    # ── API pública (bloqueante) ───────────────────────────────────────────────

    def beep_short(self):
        """Confirmação curta (1 bip)."""
        self._beep(2000, 80)

    def beep_ok(self):
        """Confirmação positiva (2 bips curtos)."""
        for _ in range(2):
            self._beep(2500, 80)
            utime.sleep_ms(80)

    def beep_error(self):
        """Erro (tom grave longo)."""
        self._beep(400, 400)

    def beep_alarm(self):
        """Alarme de medicação (3 bips distintos)."""
        for f in (2000, 2500, 3000):
            self._beep(f, 150)
            utime.sleep_ms(100)

    def beep_confirm(self):
        """Dose confirmada (melodia ascendente)."""
        for f in (1000, 1500, 2000, 2500):
            self._beep(f, 80)
            utime.sleep_ms(40)

    def silence(self):
        self._pwm.duty_u16(0)

    def deinit(self):
        self.silence()
        self._pwm.deinit()
