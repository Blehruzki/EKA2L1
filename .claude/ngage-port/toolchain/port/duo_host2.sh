#!/bin/bash
# duo_host2.sh <window> <shot prefix>: Colin McRae with the launcher answering
# "Bluetooth on" (E1184): MULTIPLAYER, its DRIVER SELECT as a guest, a tag,
# then Host a Game and Returns while the joiner comes in (E1186).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 200); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Right; at 31; k Return; at 38; k Return; at 42; k 2; at 45; k 2; at 48; k 3; at 52; k Return
at 58; k Return
at 84; k Return; at 92; k Return; at 100; k Return; at 108; k Return; at 116; k Return; at 124; k Return; at 132; k Return
wait
