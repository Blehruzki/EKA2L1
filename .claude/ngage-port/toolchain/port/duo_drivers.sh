#!/bin/bash
# duo_drivers.sh <window> <shot prefix>: Colin McRae, the main menu's tab left
# of RALLY ("...ERS", never opened before E1172), and what Return there offers.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 90); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Left; at 32; k Return; at 38; k Down; at 42; k Down; at 46; k Up; at 48; k Up; at 52; k Return; at 60; k Return
wait
