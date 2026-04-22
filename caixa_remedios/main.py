# main.py — Loop principal do sistema de alarme de remédios
# Raspberry Pi Pico + MicroPython

import sys
sys.path.append('/caixa_remedios/lib')  # garante lib/ no path

from machine import I2C, Pin
import utime
import config

from rtc_ds1307  import RTC
from display_oled import OLEDDisplay
from keypad       import Keypad
from servo_ctrl   import Servo
from motor_ctrl   import DCMotor
from pir_sensor   import PIRSensor
from led_ctrl     import LEDController
from buzzer       import Buzzer
from alarm_manager import AlarmManager
from menu          import Menu, KEY_OK, KEY_BACK

# ─── Estados do sistema ───────────────────────────────────────────────────────
ST_NORMAL   = 0   # mostra relógio, aguarda alarme ou tecla
ST_ALARM    = 1   # alarme ativo, aguarda confirmação
ST_MENU     = 2   # usuário no menu de configuração
ST_CONFIRM  = 3   # feedback pós-confirmação

# ─── Inicialização de hardware ────────────────────────────────────────────────

def init_hardware():
    i2c = I2C(config.I2C_ID, sda=Pin(config.I2C_SDA_PIN),
                 scl=Pin(config.I2C_SCL_PIN),
                 freq=config.I2C_FREQ)

    rtc     = RTC(i2c)
    display = OLEDDisplay(i2c)
    keypad  = Keypad()
    servo   = Servo()
    motor   = DCMotor()
    pir     = PIRSensor()
    leds    = LEDController()
    buzzer  = Buzzer()
    alarms  = AlarmManager()
    menu    = Menu(display, alarms, rtc, buzzer)

    return rtc, display, keypad, servo, motor, pir, leds, buzzer, alarms, menu


def safe_init():
    """Inicializa com retry e mensagem de erro no serial se falhar."""
    try:
        return init_hardware()
    except Exception as e:
        print(f"[ERRO init] {e}")
        # pisca LED embutido do Pico como indicador de falha
        led = Pin(25, Pin.OUT)
        for _ in range(10):
            led.toggle()
            utime.sleep_ms(200)
        raise


# ─── Lógica de alarme ─────────────────────────────────────────────────────────

def trigger_alarm(med_idx, time_str, display, leds, buzzer, servo, motor, alarm_mgr):
    """Ativa todos os indicadores para o remédio med_idx."""
    print(f"[ALARME] Remedio {med_idx+1} ({alarm_mgr.med_name(med_idx)}) às {time_str}")
    alarm_mgr.set_active(med_idx, time_str)
    leds.blink(med_idx)
    buzzer.beep_alarm()
    servo.open()
    motor.advance_slot(med_idx + 1)  # posiciona compartimento correto


def dismiss_alarm(med_idx, time_str, display, leds, servo, motor, buzzer, alarm_mgr):
    """Desativa alarme após confirmação do usuário."""
    alarm_mgr.confirm_dose(med_idx, time_str)
    alarm_mgr.clear_active()
    leds.off(med_idx)
    servo.close()
    motor.home()
    buzzer.beep_confirm()
    display.show_message("Dose registrada", alarm_mgr.med_name(med_idx)[:16])
    utime.sleep_ms(1500)


# ─── Tela normal (relógio + próximo alarme) ───────────────────────────────────

def update_clock_screen(rtc, display, alarm_mgr):
    h, m, s = rtc.now()
    hhmm = f"{h:02d}:{m:02d}"
    time_str = f"  {h:02d}:{m:02d}:{s:02d}  "
    nxt = alarm_mgr.next_alarm(hhmm)
    if nxt:
        next_str = f"Prox:{nxt[1]} {alarm_mgr.med_name(nxt[0])[:6]}"
    else:
        next_str = "Sem alarmes"
    display.show_clock(time_str, next_str)


# ─── Loop principal ───────────────────────────────────────────────────────────

def main():
    print("[Sistema] Iniciando caixa de remedios...")

    rtc, display, keypad, servo, motor, pir, leds, buzzer, alarm_mgr, menu = safe_init()

    display.show_message("Caixa Remedios", "Iniciando...", "v1.0")
    utime.sleep_ms(1500)
    display.clear()

    state     = ST_NORMAL
    last_min  = -1          # evita verificar alarme múltiplas vezes no mesmo minuto
    alarm_queue = []        # alarmes pendentes neste minuto

    while True:
        try:
            # ── leitura de entradas ───────────────────────────────────────────
            key      = keypad.scan()
            presence = pir.poll()
            h, m, s  = rtc.now()

            # ── gestão do display por presença ────────────────────────────────
            if presence:
                display.on()
            elif pir.display_should_sleep() and state == ST_NORMAL:
                display.off()

            # ── atualização de LEDs (blink) ───────────────────────────────────
            leds.update()

            # ── virada de meia-noite: reseta confirmações ─────────────────────
            if h == 0 and m == 0 and s < 5:
                alarm_mgr.reset_daily()

            # ── verificação de alarmes (apenas uma vez por minuto) ────────────
            if m != last_min and state == ST_NORMAL:
                last_min = m
                triggered = alarm_mgr.check_trigger(h, m)
                alarm_queue.extend(triggered)

            # ────────────────────────────────────────────────────────────────
            # MÁQUINA DE ESTADOS
            # ────────────────────────────────────────────────────────────────

            if state == ST_NORMAL:
                # despacha próximo alarme da fila
                if alarm_queue:
                    med_idx, t_str = alarm_queue.pop(0)
                    trigger_alarm(med_idx, t_str, display, leds, buzzer, servo, motor, alarm_mgr)
                    state = ST_ALARM

                elif key == 'D':   # tecla D abre menu
                    menu.open()
                    display.on()
                    state = ST_MENU

                else:
                    if display.is_on:
                        update_clock_screen(rtc, display, alarm_mgr)

            elif state == ST_ALARM:
                aa = alarm_mgr.active_alarm
                if aa:
                    name = alarm_mgr.med_name(aa["med_idx"])
                    display.show_alarm(name, f"{h:02d}:{m:02d}")

                    # pisca buzzer a cada 10 s se não confirmado
                    if s % 10 == 0 and s != 0:
                        buzzer.beep_alarm()

                    # confirmação pelo usuário
                    if key == KEY_OK:
                        dismiss_alarm(aa["med_idx"], aa["time_str"],
                                      display, leds, servo, motor, buzzer, alarm_mgr)
                        state = ST_NORMAL

                    # timeout: realarma na próxima verificação
                    elif alarm_mgr.active_timed_out():
                        print(f"[TIMEOUT] Alarme não confirmado: {aa['time_str']}")
                        buzzer.beep_error()
                        alarm_mgr.clear_active()
                        leds.off(aa["med_idx"])
                        servo.close()
                        motor.home()
                        state = ST_NORMAL

                    # próximo alarme na fila
                    elif alarm_queue:
                        next_med, next_t = alarm_queue.pop(0)
                        leds.blink(next_med)  # indica também o próximo

            elif state == ST_MENU:
                if key:
                    menu.handle_key(key)
                if not menu.is_open():
                    state = ST_NORMAL
                    display.clear()

            # ── delay do loop ─────────────────────────────────────────────────
            utime.sleep_ms(config.LOOP_SLEEP_MS)

        except KeyboardInterrupt:
            break
        except Exception as e:
            # falha não-crítica: registra e continua
            print(f"[ERRO loop] {type(e).__name__}: {e}")
            utime.sleep_ms(500)

    # ── shutdown limpo ────────────────────────────────────────────────────────
    leds.all_off()
    servo.close()
    motor.stop()
    buzzer.silence()
    display.clear()
    print("[Sistema] Encerrado.")


# ─── entry point ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    main()
