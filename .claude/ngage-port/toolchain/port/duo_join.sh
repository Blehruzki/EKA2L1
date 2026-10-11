#!/bin/bash
# duo_join.sh <window> <shot prefix>: Colin McRae multiplayer the way a player
# goes (E1184-E1187): MULTIPLAYER, its DRIVER SELECT as a guest with a tag,
# then Join a Game, the host, and Ready in the lobby; then the briefing's Continue, RACE, and the throttle (5) held.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 260); do at $t; shot $(printf %03d $t)s; done ) &
at 29; k Right; at 32; k Return; at 39; k Return; at 43; k 2; at 46; k 3; at 49; k 4; at 53; k Return
at 60; k Down; at 61; k Return; at 72; k Return   # Join a Game, select the host: the lobby
at 108; k Down; at 109; k Down; at 110; k Left; at 111; k Return   # Ready: Yes
at 134; k Return   # Continue on the briefing
at 140; k Right; at 141; k Right; at 142; k Right; at 144; k Return; at 148; k Return; at 152; k Return
at 165; timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool keydown --window "$W" 5
at 225; timeout 5 xdotool keyup --window "$W" 5
wait
