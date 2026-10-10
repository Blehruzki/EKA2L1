#!/bin/bash
# duo_profile.sh <window> <shot prefix>: Colin McRae, create a driver profile
# (RALLY -> mode -> difficulty -> DRIVER SELECT, Up to Create New Driver Profile -> TAG ENTRY "aad"; E972-E973, E1129).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 120); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return; at 42; k Up; at 43; k Return
at 46; k 2; at 49; k 2; at 52; k 3; at 56; k Return
at 64; k 2; at 67; k 3; at 71; k Return
at 78; k Down; at 79; k Down; at 80; k Down; at 82; k Return; at 88; k Return; at 94; k Return
wait
