#!/bin/bash
# duo_join_ap.sh <window> <shot prefix>: as duo_join.sh to the race, then the
# autopilot (autopilot.py) drives the stage instead of a held throttle.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 10 700); do at $t; shot $(printf %03d $t)s; done ) &
at 29; k Right; at 32; k Return; at 39; k Return; at 43; k 2; at 46; k 3; at 49; k 4; at 53; k Return
at 60; k Down; at 61; k Return; at 72; k Return   # Join a Game, select the host: the lobby
at 108; k Down; at 109; k Down; at 110; k Left; at 111; k Return   # Ready: Yes
at 134; k Return   # Continue on the briefing
at 140; k Right; at 141; k Right; at 142; k Right; at 144; k Return; at 148; k Return; at 152; k Return
# B starts AP_B_DELAY s (default 40) after A's autopilot: started 4 s apart the
# two cars met at the stone-building hairpin side by side and pushed each other
# into the barriers (E1260-E1261), a car solo never meets.
at $((190 + ${AP_B_DELAY:-40})); AP_PATH="${AP_PATH_B-$AP_PATH}" AP_CAR="${AP_CAR_B:-0}" AP_CARFILE="${AP_CARFILE_B:-$AP_CARFILE}" AP_ROLE=join python3 "$(dirname "$0")/autopilot.py" "$W" "${AP_B_SECS:-${AP_SECS:-480}}" "${O}_ap.log"   # AP_B_SECS=0: B sits at the start
# B_PAUSE=1 (with AP_B_SECS=0): B opens its pause menu (F2) once the race is
# on, so its emulator draws a menu instead of the stage and A's has more of
# the machine -- if a multiplayer pause is B's alone (E1255 tries it).
[ -n "$B_PAUSE" ] && { at 200; k F2; }
wait
