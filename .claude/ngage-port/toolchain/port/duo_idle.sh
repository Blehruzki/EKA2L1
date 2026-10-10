#!/bin/bash
# duo_idle.sh <window> <shot prefix>: no keys, a frame every 4 s (the other instance's calibration).
W=$1; O=$2; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
for t in $(seq 8 4 120); do at $t; shot $(printf %03d $t)s; done
