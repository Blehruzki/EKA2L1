#!/bin/bash
# duo_host.sh <window> <shot prefix>: Colin McRae multiplayer the way a player
# goes (E1184-E1187): MULTIPLAYER, its DRIVER SELECT as a guest with a tag,
# then Host a Game, MODE SELECT Rally and SINGLE RALLY's OK once the joiner is in, and
# Start after its Ready; then the briefing's Continue, RACE, and the throttle (5) held.
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 300); do at $t; shot $(printf %03d $t)s; done ) &
at 28; k Right; at 31; k Return; at 38; k Return; at 42; k 2; at 45; k 2; at 48; k 3; at 52; k Return
at 58; k Return    # Host a Game: HOST GAME, waiting
at 84; k Return    # the joiner is in: MODE SELECT
at 92; k Return    # Rally: SINGLE RALLY
at 100; k Down; at 101; k Down; at 102; k Return   # OK: the settings go out, the lobby
at 120; k Down; at 121; k Down; at 122; k Return   # Start, after the joiner's Ready
at 134; k Return   # Continue on the briefing
at 140; k Right; at 141; k Right; at 142; k Right; at 144; k Return; at 148; k Return; at 152; k Return
at 195; timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool keydown --window "$W" 5
at 265; timeout 5 xdotool keyup --window "$W" 5
wait
