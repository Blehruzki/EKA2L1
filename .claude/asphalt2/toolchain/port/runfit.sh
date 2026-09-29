#!/bin/bash
# runfit.sh -- build and run the scaler harness.
#
# The first run is the size that ships (MAP_MAX in gate6.cpp, 1024 since
# phase 3) and is the gate: exit status is the number of distinct failures,
# so a non-zero exit means do not commit. The second is the old 320, kept so
# the phase 3 change keeps showing its own reason -- four panels whose
# picture was sized by an array bound rather than by the screen.
set -e
P="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/fittest"
c++ -O1 -Wall -Wextra -o "$OUT" "$P/fittest.cpp"

echo "######## as shipped (mapMax 1024)"
"$OUT" -q 1024
RC=$?

echo
echo "######## the full table"
"$OUT" v 1024 | sed -n '/^-- 1:1/,/^-- inset/p'

echo "######## for comparison, the pre-phase-3 map size (320)"
"$OUT" -q 320 | sed -n '/distinct failure/,$p' || true

exit $RC
