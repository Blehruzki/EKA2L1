#!/bin/bash
# duo_drivers2.sh <window> <shot prefix>: Colin McRae, load the driver from
# DRIVERS (E1175's keys), one Back to its menu, then the tab row (Right twice)
# instead of a second Back, and Return on what that reaches (E1179).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 100); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Left; at 32; k Return; at 38; k Return; at 46; k Return
at 52; k F2; at 58; k Right; at 62; k Right; at 66; k Return; at 72; k Return
wait
