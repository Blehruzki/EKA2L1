#!/bin/bash
# duo_host.sh <window> <shot prefix>: Colin McRae to Host a Game (host2.sh's keys).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
# A frame every 4 s to 120 s, beside the keys: what each screen is when.
( for t in $(seq 8 2 300); do at $t; shot $(printf %03d $t)s; done ) &
# DRIVERS first (E1172, E1175): the tab, its menu, Load Driver Profile, select
# the saved driver; Back to MAIN MENU; then Right twice past RALLY to MULTIPLAYER.
at 28; k Left; at 32; k Return; at 38; k Return; at 46; k Return; at 54; k F2; at 58; k F2; at 62; k F2; at 66; k Right
at 68; k Right; at 71; k Return; at 75; k Return; at 80; k Return; at 85; k Return; at 100; k Return
at 110; k Down; at 111; k Down; at 112; k Return
at 126; k Down; at 127; k Down; at 128; k Return   # Start, in the host's lobby (E1160)
# The rally briefing comes up on both at Start (E1161): Continue, then 5 is
# the throttle (BUGBOOK 13.p), held through the stage start.
at 140; k Return   # Continue; then the stage, past REPAIR (E1163, duo_loadrace.sh)
at 146; k Right; at 147; k Right; at 148; k Right; at 150; k Return; at 154; k Return; at 158; k Return
at 175; timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool keydown --window "$W" 5
at 235; timeout 5 xdotool keyup --window "$W" 5
wait
