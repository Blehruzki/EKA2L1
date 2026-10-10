#!/bin/bash
# duo_host_vlate.sh <window> <shot prefix>: host for duo_racejoin.sh -- the menu
# nudged every 40 s against the attract demo (E1138), Host from 150 s, MODE
# SELECT, OK on SINGLE RALLY, then Start in the host's lobby (E1154).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 270); do at $t; shot $(printf %03d $t)s; done ) &
for t in 40 80 120; do at $t; k Right; at $((t+1)); k Left; done
at 150; k Right; at 153; k Return; at 157; k Return; at 162; k Return; at 167; k Return
at 190; k Return
at 196; k Down; at 197; k Down; at 198; k Return
at 212; k Down; at 213; k Down; at 214; k Return
wait
