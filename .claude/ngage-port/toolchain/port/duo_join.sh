#!/bin/bash
# duo_join.sh <window> <shot prefix>: Colin McRae to Join a Game, after the host is up.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
# A frame every 4 s to 120 s, beside the keys: what each screen is when.
( for t in $(seq 8 2 300); do at $t; shot $(printf %03d $t)s; done ) &
# DRIVERS first: Load Driver Profile, select the saved driver, Back twice to
# MAIN MENU (E1172), then Right twice past RALLY to MULTIPLAYER.
at 28; k Left; at 32; k Return; at 46; k Return; at 54; k F2; at 58; k F2; at 62; k Right
at 65; k Right; at 68; k Return; at 72; k Return; at 77; k Return; at 82; k Down; at 83; k Return; at 89; k Return
at 114; k Down; at 115; k Down; at 116; k Left; at 117; k Return   # after the host's OK (E1144)
# The rally briefing comes up on both at Start (E1161): Continue, then 5 is
# the throttle (BUGBOOK 13.p), held through the stage start.
at 136; k Return   # Continue; then the stage, past REPAIR (E1163, duo_loadrace.sh)
at 142; k Right; at 143; k Right; at 144; k Right; at 146; k Return; at 150; k Return; at 154; k Return
at 171; timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool keydown --window "$W" 5
at 231; timeout 5 xdotool keyup --window "$W" 5
wait
