#!/bin/bash
# benchplay.sh -- play One on the bench while emurun.sh runs it (round 132).
#
#   benchplay.sh &  GAME=one TMO=360 ./emurun.sh "..."
#
# The emulator takes keys only through XTEST after a click inside its window
# (xdotool key --window is ignored by Qt), so: a click on the screen, then the
# menu -- PLAY, SINGLE-PLAYER, VERSUS, CONTINUE, the first opponent, the first
# location -- with Return, which the default bindings map to the centre key
# (EStdKeyDevice3, 0xa7). Then a screenshot every EVERY seconds and, if PRESS
# is set, that key too: Return answers the score screen's RETRY, so the bench
# plays match after match. Left alone instead, the title runs its attract demo.
#   START  seconds before the first key (the title must be up)   default 24
#   SHOTS  screenshots, EVERY seconds apart                       30, 10
#   PRESS  a key after each shot ("" for none)
#   OUT    where shots go (shot-N.png, cropped to the phone screen)
export DISPLAY=:99
OUT="${OUT:-${EMUSCRATCH:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad}/play}"
mkdir -p "$OUT"
tap() { for k in "$@"; do case "$k" in
    wait*) sleep "${k#wait}";;
    *) xdotool keydown "$k"; sleep 0.15; xdotool keyup "$k"; sleep 0.8;;
  esac; done; }
shot() { import -display :99 -window root "$OUT/full-$1.png" &&
         convert "$OUT/full-$1.png" -crop 490x660+210+40 -resize 50% "$OUT/shot-$1.png"; }
sleep "${START:-24}"
xdotool mousemove 450 300 click 1; sleep 0.5
tap Return wait2 Return wait2 Return wait3 Return wait3 Return wait3 Return
for i in $(seq 0 $(( ${SHOTS:-30} - 1 ))); do
  sleep "${EVERY:-10}"
  shot "$i"
  [ -n "$PRESS" ] && tap $PRESS
done
