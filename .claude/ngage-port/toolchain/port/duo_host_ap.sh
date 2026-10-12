#!/bin/bash
# duo_host_ap.sh <window> <shot prefix>: as duo_host.sh to the race, then the
# autopilot (autopilot.py) drives the stage instead of a held throttle, so a run
# can reach the splits and the finish.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 10 700); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Right; at 31; k Return; at 38; k Return; at 42; k 2; at 45; k 2; at 48; k 3; at 52; k Return
at 58; k Return    # Host a Game: HOST GAME, waiting
at 84; k Return    # the joiner is in: MODE SELECT
at 92; k Return    # Rally: SINGLE RALLY
at 100; k Down; at 101; k Down; at 102; k Return   # OK: the settings go out, the lobby
at 120; k Down; at 121; k Down; at 122; k Return   # Start, after the joiner's Ready
at 134; k Return   # Continue on the briefing
at 140; k Right; at 141; k Right; at 142; k Right; at 144; k Return; at 148; k Return; at 152; k Return
at 190; AP_CARFILE="${AP_CARFILE_A:-$AP_CARFILE}" AP_ROLE=host python3 "$(dirname "$0")/autopilot.py" "$W" "${AP_SECS:-480}" "${O}_ap.log"
wait
