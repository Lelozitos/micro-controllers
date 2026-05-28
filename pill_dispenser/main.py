# main.py — Main loop for the medication alarm system
# Raspberry Pi Pico + MicroPython

import sys
sys.path.append('/lib')

from machine import I2C, Pin
import utime
import config

from rtc_ds1307   import RTC
from display_oled import OLEDDisplay
from keypad       import Keypad
from servo_ctrl   import Servo
# from pir_sensor import PIRSensor
from led_ctrl     import LEDController
# from buzzer     import Buzzer
from alarm_manager import AlarmManager
from menu          import Menu, KEY_OK, KEY_BACK

ST_NORMAL = 0
ST_ALARM  = 1
ST_MENU   = 2


def init_hardware():
    i2c = I2C(config.I2C_ID, sda=Pin(config.I2C_SDA_PIN),
                 scl=Pin(config.I2C_SCL_PIN),
                 freq=config.I2C_FREQ)

    rtc     = RTC(i2c)
    display = OLEDDisplay(i2c)
    keypad  = Keypad()
    servo   = Servo()
    # pir   = PIRSensor()
    leds    = LEDController()
    # buzzer = Buzzer()
    alarms  = AlarmManager()
    menu    = Menu(display, alarms, rtc)

    return rtc, display, keypad, servo, leds, alarms, menu


def safe_init():
    try:
        return init_hardware()
    except Exception as e:
        print(f"[ERRO INICIALIZACAO] {e}")
        led = Pin(25, Pin.OUT)
        for _ in range(10):
            led.toggle()
            utime.sleep_ms(200)
        raise


def trigger_alarm(med_idx, time_str, display, leds, servo, alarm_mgr):
    print(f"[ALARME] Remedio {med_idx+1} ({alarm_mgr.med_name(med_idx)}) as {time_str}")
    alarm_mgr.set_active(med_idx, time_str)
    leds.blink(med_idx)
    # buzzer.beep_alarm()
    servo.open()


def dismiss_alarm(med_idx, time_str, display, leds, servo, alarm_mgr, servo_opened):
    alarm_mgr.confirm_dose(med_idx, time_str)
    alarm_mgr.clear_active()
    leds.off(med_idx)
    if servo_opened:
        servo.close()
    # buzzer.beep_confirm()
    display.show_message("Dose tomada", alarm_mgr.med_name(med_idx)[:16])
    utime.sleep_ms(1500)


def update_clock_screen(rtc, display, alarm_mgr):
    h, m, s = rtc.now()
    hhmm = f"{h:02d}:{m:02d}"
    time_str = f"  {h:02d}:{m:02d}:{s:02d}  "
    nxt = alarm_mgr.next_alarm(hhmm)
    next_str = f"Prox:{nxt[1]} {alarm_mgr.med_name(nxt[0])[:6]}" if nxt else "Sem alarmes"
    display.show_clock(time_str, next_str)


def main():
    print("[System] Iniciando dispenser...")

    rtc, display, keypad, servo, leds, alarm_mgr, menu = safe_init()

    display.show_message("Remedios", "Iniciando...", "v1.0")
    utime.sleep_ms(1500)
    display.clear()

    state        = ST_NORMAL
    last_min     = -1
    alarm_queue  = []
    servo_opened = False

    while True:
        try:
            key     = keypad.scan()
            # presence = pir.poll()
            h, m, s = rtc.now()

            leds.update()

            if h == 0 and m == 0 and s < 5:
                alarm_mgr.reset_daily()

            if m != last_min and state == ST_NORMAL:
                last_min = m
                alarm_queue.extend(alarm_mgr.check_trigger(h, m))

            if state == ST_NORMAL:
                if alarm_queue:
                    med_idx, t_str = alarm_queue.pop(0)
                    trigger_alarm(med_idx, t_str, display, leds, servo, alarm_mgr)
                    servo_opened = True
                    state = ST_ALARM
                elif key == 'D':
                    menu.open()
                    state = ST_MENU
                else:
                    update_clock_screen(rtc, display, alarm_mgr)

            elif state == ST_ALARM:
                aa = alarm_mgr.active_alarm
                if aa:
                    display.show_alarm(alarm_mgr.med_name(aa["med_idx"]), f"{h:02d}:{m:02d}")

                    # if presence and not servo_opened:
                    #     servo.open()
                    #     servo_opened = True

                    # buzzer.beep_alarm()  # repetir a cada 10s

                    if key == KEY_OK:
                        dismiss_alarm(aa["med_idx"], aa["time_str"],
                                      display, leds, servo, alarm_mgr, servo_opened)
                        servo_opened = False
                        state = ST_NORMAL
                    elif alarm_mgr.active_timed_out():
                        print(f"[TIMEOUT] {aa['time_str']} — dose nao confirmada")
                        # buzzer.beep_error()
                        alarm_mgr.clear_active()
                        leds.off(aa["med_idx"])
                        if servo_opened:
                            servo.close()
                        servo_opened = False
                        state = ST_NORMAL
                    elif alarm_queue:
                        leds.blink(alarm_queue[0][0])

            elif state == ST_MENU:
                if key:
                    menu.handle_key(key)
                else:
                    menu.tick()
                if not menu.is_open():
                    state = ST_NORMAL
                    display.clear()

            utime.sleep_ms(config.LOOP_SLEEP_MS)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[ERRO NO LOOP] {type(e).__name__}: {e}")
            utime.sleep_ms(500)

    leds.all_off()
    servo.close()
    # buzzer.silence()
    display.clear()
    print("[Sistema] Desligando...")


if __name__ == "__main__":
    main()
