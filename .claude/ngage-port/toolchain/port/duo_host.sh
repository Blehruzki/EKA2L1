#!/bin/bash
# duo_host.sh <window> <shot prefix>: Colin McRae to Host a Game (host2.sh's keys).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
# A frame every 4 s to 120 s, beside the keys: what each screen is when.
( for t in $(seq 8 4 180); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Right; at 31; k Return; at 35; k Return; at 40; k Return; at 45; k Return; at 60; k Return
at 70; k Down; at 71; k Down; at 72; k Return
at 86; k Down; at 87; k Down; at 88; k Return   # Start, in the host's lobby (E1160)
for t in 70 85 100 115 130 150 170; do at $t; shot $t; done
wait
