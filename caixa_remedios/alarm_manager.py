# alarm_manager.py — Gerenciamento de alarmes e confirmação de doses

import ujson
import utime
import config


def _default_alarms():
    """Estrutura padrão: 4 remédios, sem alarmes configurados."""
    return {
        str(i): {
            "name": config.MED_NAMES[i],
            "alarms": [],       # lista de "HH:MM"
            "enabled": True,
        }
        for i in range(config.MAX_MEDS)
    }


class AlarmManager:
    """
    Persiste alarmes em JSON na memória flash do Pico.
    Controla estado de alarme ativo e confirmação de dose.
    """

    def __init__(self):
        self._data     = {}
        self._load()
        # estado de runtime (não persiste)
        self._active_alarm  = None   # {med_idx, time_str, fired_at}
        self._confirmed     = set()  # alarmes já confirmados hoje: "HH:MM-idx"

    # ── persistência ─────────────────────────────────────────────────────────

    def _load(self):
        try:
            with open(config.ALARM_FILE, "r") as f:
                self._data = ujson.load(f)
        except Exception:
            self._data = _default_alarms()
            self._save()

    def _save(self):
        try:
            with open(config.ALARM_FILE, "w") as f:
                ujson.dump(self._data, f)
        except Exception:
            pass  # falha silenciosa; próxima inicialização vai recriar

    # ── manipulação de alarmes ────────────────────────────────────────────────

    def get_med(self, idx: int) -> dict:
        return self._data.get(str(idx), {})

    def set_med_name(self, idx: int, name: str):
        self._data[str(idx)]["name"] = name[:20]
        self._save()

    def add_alarm(self, med_idx: int, time_str: str) -> bool:
        """Adiciona alarme 'HH:MM'. Retorna False se já está no limite."""
        med = self._data[str(med_idx)]
        if time_str in med["alarms"]:
            return True
        if len(med["alarms"]) >= config.MAX_ALARMS_EACH:
            return False
        med["alarms"].append(time_str)
        med["alarms"].sort()
        self._save()
        return True

    def remove_alarm(self, med_idx: int, time_str: str):
        med = self._data[str(med_idx)]
        if time_str in med["alarms"]:
            med["alarms"].remove(time_str)
            self._save()

    def toggle_med(self, med_idx: int):
        key = str(med_idx)
        self._data[key]["enabled"] = not self._data[key]["enabled"]
        self._save()

    def all_alarms_flat(self) -> list:
        """Retorna lista de (med_idx, time_str) ordenada por horário."""
        result = []
        for idx in range(config.MAX_MEDS):
            med = self._data[str(idx)]
            if med["enabled"]:
                for t in med["alarms"]:
                    result.append((idx, t))
        result.sort(key=lambda x: x[1])
        return result

    def next_alarm(self, current_hhmm: str) -> tuple | None:
        """Retorna (med_idx, time_str) do próximo alarme após current_hhmm."""
        alarms = self.all_alarms_flat()
        for idx, t in alarms:
            if t > current_hhmm:
                return idx, t
        return alarms[0] if alarms else None  # wrap-around

    # ── detecção de disparo ───────────────────────────────────────────────────

    def check_trigger(self, h: int, m: int) -> list:
        """
        Verifica se algum alarme deve disparar agora.
        Retorna lista de (med_idx, time_str) a disparar.
        """
        hhmm = f"{h:02d}:{m:02d}"
        triggered = []
        for idx in range(config.MAX_MEDS):
            med = self._data[str(idx)]
            if not med["enabled"]:
                continue
            for t in med["alarms"]:
                key = f"{t}-{idx}"
                if t == hhmm and key not in self._confirmed:
                    triggered.append((idx, t))
        return triggered

    def confirm_dose(self, med_idx: int, time_str: str):
        """Marca dose como tomada para este ciclo."""
        self._confirmed.add(f"{time_str}-{med_idx}")

    def reset_daily(self):
        """Limpa confirmações (chamar na virada da meia-noite)."""
        self._confirmed.clear()

    # ── alarme ativo ─────────────────────────────────────────────────────────

    def set_active(self, med_idx: int, time_str: str):
        self._active_alarm = {
            "med_idx": med_idx,
            "time_str": time_str,
            "fired_at": utime.ticks_ms(),
        }

    def clear_active(self):
        self._active_alarm = None

    @property
    def active_alarm(self):
        return self._active_alarm

    def active_timed_out(self) -> bool:
        if not self._active_alarm:
            return False
        elapsed = utime.ticks_diff(utime.ticks_ms(), self._active_alarm["fired_at"])
        return elapsed > config.CONFIRM_TIMEOUT * 1000

    def med_name(self, idx: int) -> str:
        return self._data[str(idx)]["name"]
