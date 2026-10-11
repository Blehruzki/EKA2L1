#!/bin/bash
# duo_rallyload.sh <window> <shot prefix>: Colin McRae, choose the driver the way
# a single-player rally does -- RALLY, MODE SELECT, DIFFICULTY, DRIVER SELECT,
# Load Driver Profile, select it, Back to DRIVER SELECT and Play As the driver
# (E1181) -- then Back to MAIN MENU.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 120); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return
at 44; k Up; at 45; k Up; at 46; k Return; at 54; k Return
at 62; k F2; at 68; k Return; at 76; k F2; at 82; k F2; at 86; k F2; at 90; k F2
wait
