#!/bin/bash
# duo_racejoin.sh <window> <shot prefix>: Colin McRae, load the driver profile
# and drive into a stage with it (E973-E976's Returns), so the game saves it
# as the current driver in gameinfo.dat (E1145); then quit the stage through
# its PAUSED menu (the softkey, E979) and go to MULTIPLAYER, Join, keeping the
# driver valid in-session (E1155). Calibration frames every 3 s.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 3 270); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return
at 42; k Up; at 43; k Up; at 44; k Return; at 48; k Return
for t in 54 58 62 66 70 74 78 82; do at $t; k Return; done
at 88; k Right; at 89; k Right; at 90; k Right; at 92; k Return; at 96; k Return; at 100; k Return
at 128; k F2; at 131; k Left; at 132; k Return; at 134; k Up; at 135; k Return   # PAUSED: EXIT, Quit game (E1156-E1158)
at 156; k Right; at 159; k Return; at 163; k Return; at 168; k Return; at 173; k Down; at 174; k Return
at 180; k Return
at 204; k Down; at 205; k Down; at 206; k Left; at 207; k Return
wait
