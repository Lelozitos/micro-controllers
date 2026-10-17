#!/bin/bash
# Starts the dashboard and the race timer together in this one terminal.
# Ctrl+C stops both (and the timer turns its LEDs off).
# Usage: ./start.sh

cd "$(dirname "$(readlink -f "$0")")" || exit 1

# Only one copy of each can run: stop any left over from before.
sudo pkill -x race_timer 2>/dev/null
pkill -f "node server.js" 2>/dev/null
sleep 0.5

# Build the timer if it hasn't been built yet.
if [ ! -x build/race_timer ]; then
    echo "Building race_timer..."
    mkdir -p build && (cd build && cmake .. >/dev/null && make) || exit 1
fi

# First run: install the dashboard's packages.
if [ ! -d dashboard/node_modules ]; then
    echo "Installing dashboard packages..."
    (cd dashboard && npm install) || exit 1
fi

# Dashboard in the background, its lines prefixed with [dashboard].
(cd dashboard && exec env MQTT_HOST=localhost node server.js) \
    > >(sed -u 's/^/[dashboard] /') 2>&1 &
DASH_PID=$!

stop_dashboard() {
    kill "$DASH_PID" 2>/dev/null
    wait "$DASH_PID" 2>/dev/null
}
trap stop_dashboard EXIT

sleep 1
IP=$(hostname -I | awk '{print $1}')
echo "Dashboard: http://${IP:-localhost}:3000   (Ctrl+C stops everything)"
echo

# Timer in the foreground; asks for the sudo password if needed.
# It reads racers.csv from this folder.
sudo ./build/race_timer
