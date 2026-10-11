#!/bin/bash
# duo_guest.sh <window> <shot prefix>: Colin McRae, RALLY's DRIVER SELECT as a
# guest (its default), the tag entry, then Back to MAIN MENU (E1183).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 120); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return; at 46; k Return
at 52; k 2; at 55; k 2; at 58; k 3; at 62; k Return; at 66; k Return
at 74; k F2; at 78; k F2; at 82; k F2; at 86; k F2; at 90; k F2
wait
