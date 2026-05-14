# menu.py — Sistema de menu interativo via teclado + OLED

import utime
import config

# Teclas de navegação
KEY_UP     = 'A'  # subir / incrementar
KEY_DOWN   = 'B'  # descer / decrementar
KEY_OK     = '#'  # confirmar / selecionar
KEY_BACK   = '*'  # voltar / cancelar


class Menu:
    """
    Máquina de estados para menu de configuração.
    Chame handle_key() com a tecla lida pelo keypad.
    O menu se comunica com AlarmManager e OLEDDisplay injetados.
    """

    # estados internos
    _ST_IDLE        = 0
    _ST_MAIN        = 1
    _ST_SEL_MED     = 2
    _ST_MED_OPT     = 3
    _ST_ADD_ALARM   = 4
    _ST_LIST_ALARMS = 5
    _ST_SET_TIME    = 6
    _ST_SET_DATE    = 7
    _ST_CONFIRM_DEL = 8

    def __init__(self, display, alarm_mgr, rtc, buzzer):
        self._d  = display
        self._am = alarm_mgr
        self._rtc = rtc
        self._bz  = buzzer

        self._state     = self._ST_IDLE
        self._sel_med   = 0
        self._sel_opt   = 0
        self._input_buf = ""
        self._alarm_list = []
        self._alarm_sel  = 0
        self._last_activity = utime.ticks_ms()

    # ── entrada pública ───────────────────────────────────────────────────────

    def open(self):
        """Abre o menu principal."""
        self._state = self._ST_MAIN
        self._sel_opt = 0
        self._render()

    def is_open(self) -> bool:
        return self._state != self._ST_IDLE

    def handle_key(self, key: str):
        self._last_activity = utime.ticks_ms()
        handler = {
            self._ST_MAIN:        self._h_main,
            self._ST_SEL_MED:     self._h_sel_med,
            self._ST_MED_OPT:     self._h_med_opt,
            self._ST_ADD_ALARM:   self._h_add_alarm,
            self._ST_LIST_ALARMS: self._h_list_alarms,
            self._ST_SET_TIME:    self._h_set_time,
            self._ST_SET_DATE:    self._h_set_date,
            self._ST_CONFIRM_DEL: self._h_confirm_del,
        }.get(self._state)
        if handler:
            handler(key)

    # ── renderização ──────────────────────────────────────────────────────────

    def _render(self):
        d = self._d
        if self._state == self._ST_MAIN:
            d.show_menu("=== MENU ===", ["1-Remedios", "2-Acertar hora", "3-Acertar data"], self._sel_opt)

        elif self._state == self._ST_SEL_MED:
            # grade 2x2 — todos os 4 remédios visíveis de uma vez
            d.show_med_grid([self._am.med_name(i) for i in range(config.MAX_MEDS)])

        elif self._state == self._ST_MED_OPT:
            name = self._am.med_name(self._sel_med)[:10]
            d.show_menu(name, ["1-Add alarme", "2-Ver alarm.", "3-Ativar/Des."], self._sel_opt)

        elif self._state == self._ST_ADD_ALARM:
            d.show_input("Horario HH:MM", self._input_buf)

        elif self._state == self._ST_LIST_ALARMS:
            med  = self._am.get_med(self._sel_med)
            alms = med.get("alarms", [])
            if not alms:
                d.show_message("Sem alarmes", KEY_BACK+"=voltar")
            else:
                item = alms[self._alarm_sel] if self._alarm_sel < len(alms) else "--:--"
                d.show_menu(f"Alarm {self._alarm_sel+1}/{len(alms)}", [item, KEY_OK+"=del "+KEY_BACK+"=vol"])

        elif self._state == self._ST_SET_TIME:
            d.show_input("Hora HH:MM:SS", self._input_buf)

        elif self._state == self._ST_SET_DATE:
            d.show_input("Data DD/MM/AA", self._input_buf)

        elif self._state == self._ST_CONFIRM_DEL:
            d.show_message("Deletar?", KEY_OK+"=sim  "+KEY_BACK+"=nao")

    # ── handlers por estado ───────────────────────────────────────────────────

    def _h_main(self, key):
        if key == KEY_UP:
            self._sel_opt = (self._sel_opt - 1) % 3
        elif key == KEY_DOWN:
            self._sel_opt = (self._sel_opt + 1) % 3
        elif key == '1':
            self._state = self._ST_SEL_MED; self._sel_med = 0
        elif key == '2':
            self._state = self._ST_SET_TIME; self._input_buf = ""
        elif key == '3':
            self._state = self._ST_SET_DATE; self._input_buf = ""
        elif key == KEY_OK:
            {0: self._open_sel_med, 1: self._open_set_time, 2: self._open_set_date}.get(self._sel_opt, lambda: None)()
            return
        elif key == KEY_BACK:
            self._close(); return
        self._render()

    def _open_sel_med(self):  self._state = self._ST_SEL_MED; self._sel_med = 0; self._render()
    def _open_set_time(self): self._state = self._ST_SET_TIME; self._input_buf = ""; self._render()
    def _open_set_date(self): self._state = self._ST_SET_DATE; self._input_buf = ""; self._render()

    def _h_sel_med(self, key):
        # grade 2x2 visível — seleciona direto por número
        if key in '1234':
            self._sel_med = int(key) - 1
            self._state = self._ST_MED_OPT; self._sel_opt = 0
        elif key == KEY_BACK:
            self._state = self._ST_MAIN; self._sel_opt = 0
        self._render()

    def _h_med_opt(self, key):
        if key == '1' or (key == KEY_OK and self._sel_opt == 0):
            self._state = self._ST_ADD_ALARM; self._input_buf = ""
        elif key == '2' or (key == KEY_OK and self._sel_opt == 1):
            self._alarm_sel = 0; self._state = self._ST_LIST_ALARMS
        elif key == '3' or (key == KEY_OK and self._sel_opt == 2):
            self._am.toggle_med(self._sel_med)
            self._bz.beep_short()
            med = self._am.get_med(self._sel_med)
            status = "Ativado" if med["enabled"] else "Desativado"
            self._d.show_message(med["name"][:16], status)
            utime.sleep_ms(1200)
        elif key == KEY_UP:
            self._sel_opt = (self._sel_opt - 1) % 3
        elif key == KEY_DOWN:
            self._sel_opt = (self._sel_opt + 1) % 3
        elif key == KEY_BACK:
            self._state = self._ST_SEL_MED
        self._render()

    def _h_add_alarm(self, key):
        # aceita dígitos; backspace com *; confirma com #
        if key in '0123456789' and len(self._input_buf) < 5:
            self._input_buf += key
            # auto-insere ':'
            if len(self._input_buf) == 2:
                self._input_buf += ':'
        elif key == KEY_BACK:
            if self._input_buf:
                self._input_buf = self._input_buf[:-1]
                if self._input_buf.endswith(':'):
                    self._input_buf = self._input_buf[:-1]
            else:
                self._state = self._ST_MED_OPT
        elif key == KEY_OK and len(self._input_buf) == 5:
            if self._validate_hhmm(self._input_buf):
                ok = self._am.add_alarm(self._sel_med, self._input_buf)
                if ok:
                    self._bz.beep_ok()
                    self._d.show_message("Salvo!", self._input_buf)
                else:
                    self._bz.beep_error()
                    self._d.show_message("Limite ating.", f"Max {config.MAX_ALARMS_EACH}")
            else:
                self._bz.beep_error()
                self._d.show_message("Hora invalida")
            utime.sleep_ms(1000)
            self._state = self._ST_MED_OPT; self._input_buf = ""
        self._render()

    def _h_list_alarms(self, key):
        med  = self._am.get_med(self._sel_med)
        alms = med.get("alarms", [])
        if key == KEY_UP:
            self._alarm_sel = (self._alarm_sel - 1) % max(1, len(alms))
        elif key == KEY_DOWN:
            self._alarm_sel = (self._alarm_sel + 1) % max(1, len(alms))
        elif key == KEY_OK and alms:
            self._state = self._ST_CONFIRM_DEL
        elif key == KEY_BACK:
            self._state = self._ST_MED_OPT
        self._render()

    def _h_confirm_del(self, key):
        if key == KEY_OK:
            med  = self._am.get_med(self._sel_med)
            alms = med.get("alarms", [])
            if self._alarm_sel < len(alms):
                self._am.remove_alarm(self._sel_med, alms[self._alarm_sel])
                self._bz.beep_short()
        self._state = self._ST_LIST_ALARMS; self._alarm_sel = 0
        self._render()

    def _h_set_time(self, key):
        # formato: HH:MM:SS → 8 dígitos com auto ':'
        if key in '0123456789' and len(self._input_buf) < 8:
            self._input_buf += key
            if len(self._input_buf) in (2, 5):
                self._input_buf += ':'
        elif key == KEY_BACK:
            if self._input_buf:
                self._input_buf = self._input_buf[:-1]
                if self._input_buf.endswith(':'):
                    self._input_buf = self._input_buf[:-1]
            else:
                self._state = self._ST_MAIN
        elif key == KEY_OK and len(self._input_buf) == 8:
            try:
                parts = self._input_buf.split(':')
                h, m, s = int(parts[0]), int(parts[1]), int(parts[2])
                if 0 <= h < 24 and 0 <= m < 60 and 0 <= s < 60:
                    self._rtc.set_time(h, m, s)
                    self._bz.beep_ok()
                    self._d.show_message("Hora salva!", self._input_buf)
                else:
                    raise ValueError
            except Exception:
                self._bz.beep_error()
                self._d.show_message("Hora invalida")
            utime.sleep_ms(1000)
            self._state = self._ST_MAIN; self._input_buf = ""
        self._render()

    def _h_set_date(self, key):
        # formato: DD/MM/AA → 6 dígitos com auto '/'
        if key in '0123456789' and len(self._input_buf) < 8:
            self._input_buf += key
            if len(self._input_buf) in (2, 5):
                self._input_buf += '/'
        elif key == KEY_BACK:
            if self._input_buf:
                self._input_buf = self._input_buf[:-1]
                if self._input_buf.endswith('/'):
                    self._input_buf = self._input_buf[:-1]
            else:
                self._state = self._ST_MAIN
        elif key == KEY_OK and len(self._input_buf) == 8:
            try:
                parts = self._input_buf.split('/')
                d, m, y = int(parts[0]), int(parts[1]), int(parts[2]) + 2000
                if 1 <= d <= 31 and 1 <= m <= 12:
                    dt = self._rtc.datetime()
                    # dt: (year,month,day,weekday,hour,minute,second,0)
                    self._rtc.set_datetime(y, m, d, dt[4], dt[5], dt[6])
                    self._bz.beep_ok()
                    self._d.show_message("Data salva!", self._input_buf)
                else:
                    raise ValueError
            except Exception:
                self._bz.beep_error()
                self._d.show_message("Data invalida")
            utime.sleep_ms(1000)
            self._state = self._ST_MAIN; self._input_buf = ""
        self._render()

    # ── utilitários ───────────────────────────────────────────────────────────

    def _validate_hhmm(self, s: str) -> bool:
        try:
            parts = s.split(':')
            return 0 <= int(parts[0]) < 24 and 0 <= int(parts[1]) < 60
        except Exception:
            return False

    def _close(self):
        self._state = self._ST_IDLE
        self._d.clear()
