#!/bin/bash
# duo_loadjoin.sh <window> <shot prefix>: Colin McRae, load the driver profile
# (RALLY -> mode -> difficulty -> DRIVER SELECT, Up x2: Load Driver Profile),
# back to the main menu with the right softkey (F2), then MULTIPLAYER, Join.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 2 200); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return
at 42; k Up; at 43; k Up; at 44; k Return; at 48; k Return
at 54; k F2; at 58; k F2; at 62; k F2; at 66; k F2
# MULTIPLAYER and Join as duo_join.sh, 51 s later (after the host is up, E1136); the host listed, selected; Ready.
at 80; k Right; at 83; k Return; at 87; k Return; at 92; k Return; at 97; k Down; at 98; k Return
at 104; k Return; at 107; k Return
at 126; k Down; at 127; k Down; at 128; k Left; at 129; k Return
wait
