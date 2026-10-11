#!/bin/bash
# duo_host.sh <window> <shot prefix>: Colin McRae to Host a Game (host2.sh's keys).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
# A frame every 4 s to 120 s, beside the keys: what each screen is when.
( for t in $(seq 8 2 300); do at $t; shot $(printf %03d $t)s; done ) &
# DRIVERS first: Load Driver Profile, select the saved driver, Back twice to
# MAIN MENU (E1172), then Right twice past RALLY to MULTIPLAYER.
at 28; k Left; at 32; k Return; at 46; k Return; at 54; k F2; at 58; k F2; at 62; k Right
at 64; k Right; at 67; k Return; at 71; k Return; at 76; k Return; at 81; k Return; at 96; k Return
at 106; k Down; at 107; k Down; at 108; k Return
at 122; k Down; at 123; k Down; at 124; k Return   # Start, in the host's lobby (E1160)
# The rally briefing comes up on both at Start (E1161): Continue, then 5 is
# the throttle (BUGBOOK 13.p), held through the stage start.
at 136; k Return   # Continue; then the stage, past REPAIR (E1163, duo_loadrace.sh)
at 142; k Right; at 143; k Right; at 144; k Right; at 146; k Return; at 150; k Return; at 154; k Return
at 171; timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool keydown --window "$W" 5
at 231; timeout 5 xdotool keyup --window "$W" 5
wait
