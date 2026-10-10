#!/bin/bash
# duo_loadrace.sh <window> <shot prefix>: Colin McRae, load the driver profile
# and drive into a stage with it (E973-E976's Returns), so the game saves it
# as the current driver in gameinfo.dat (E1145).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 160); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return
at 42; k Up; at 43; k Up; at 44; k Return; at 48; k Return
for t in 54 58 62 66 70 74 78 82; do at $t; k Return; done
at 88; k Right; at 89; k Right; at 90; k Right; at 92; k Return; at 96; k Return; at 100; k Return
wait
