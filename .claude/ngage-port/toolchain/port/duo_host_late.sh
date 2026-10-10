#!/bin/bash
# duo_host_late.sh <window> <shot prefix>: duo_host.sh's keys 44 s later, for a
# joiner that first loads its driver (duo_loadjoin.sh).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 200); do at $t; shot $(printf %03d $t)s; done ) &
# The main menu starts its attract demo after about 65 s idle (E1138): nudge it.
at 40; k Right; at 41; k Left; at 60; k Right; at 61; k Left
at 72; k Right; at 75; k Return; at 79; k Return; at 84; k Return; at 89; k Return; at 112; k Return
at 116; k Down; at 117; k Down; at 118; k Return
wait
