#!/bin/bash
# duo_join.sh <window> <shot prefix>: Colin McRae to Join a Game, after the host is up.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
# A frame every 4 s to 120 s, beside the keys: what each screen is when.
( for t in $(seq 8 4 120); do at $t; shot $(printf %03d $t)s; done ) &
at 29; k Right; at 32; k Return; at 36; k Return; at 41; k Return; at 46; k Down; at 47; k Return; at 53; k Return
for t in 75 85 95 105 115 130 150 170; do at $t; shot $t; done
wait
