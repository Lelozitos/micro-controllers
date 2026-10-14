#!/bin/bash
# Lights one GPIO at a time so you can see which LED is wired where.
# Stop race_timer first (it drives the same pins): sudo pkill race_timer
# Usage: ./led_test.sh            (tests the usual LED pins)
#        ./led_test.sh 17 27 ...  (tests the BCM GPIO numbers you give)

PINS=("$@")
[ ${#PINS[@]} -eq 0 ] && PINS=(24 25 5 6 13)

# BCM GPIO -> physical header pin
declare -A PHYS=([2]=3 [3]=5 [4]=7 [5]=29 [6]=31 [7]=26 [8]=24 [9]=21 [10]=19 [11]=23
                 [12]=32 [13]=33 [14]=8 [15]=10 [16]=36 [17]=11 [18]=12 [19]=35 [20]=38
                 [21]=40 [22]=15 [23]=16 [24]=18 [25]=22 [26]=37 [27]=13)

# Never drive these: GPIO7-11 are the RC522's SPI pins (driving them takes
# them out of SPI mode until a reboot) and GPIO22/23 are the buttons (driving
# a pin high while its button shorts it to GND).
for p in "${PINS[@]}"; do
    case "$p" in
        7|8|9|10|11|22|23)
            echo "GPIO$p is used by the RC522 or a button - not testing it."
            exit 1 ;;
    esac
done

if pgrep -x race_timer >/dev/null; then
    echo "race_timer is running - stop it first: sudo pkill race_timer"
    exit 1
fi

for p in "${PINS[@]}"; do pinctrl set "$p" op dl; done
for p in "${PINS[@]}"; do
    echo "ON: GPIO$p (physical pin ${PHYS[$p]:-?}) - which LED is lit?"
    pinctrl set "$p" op dh
    sleep 3
    pinctrl set "$p" op dl
    sleep 0.5
done
echo "Done - all tested pins are off."
