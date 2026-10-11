#!/bin/bash
# duo_join2.sh <window> <shot prefix>: Colin McRae with the launcher answering
# "Bluetooth on" (E1184): MULTIPLAYER, its DRIVER SELECT as a guest, a tag,
# then Join a Game, the host, and Returns (E1186).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 200); do at $t; shot $(printf %03d $t)s; done ) &
at 29; k Right; at 32; k Return; at 39; k Return; at 43; k 2; at 46; k 3; at 49; k 4; at 53; k Return
at 60; k Down; at 61; k Return; at 72; k Return
at 88; k Return; at 96; k Return; at 104; k Return; at 112; k Return; at 120; k Return; at 128; k Return
wait
