#!/bin/bash
# runfit.sh -- build and run the scaler harness at both map sizes.
#
# The first is what ships today, the second what widening mapX/mapY to 1024
# would give. Exit status is the number of distinct failures at the shipped
# size, so this can gate a commit once they are down to zero.
set -e
P="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/fittest"
c++ -O1 -Wall -Wextra -o "$OUT" "$P/fittest.cpp"
echo "######## as shipped (mapMax 320)"
"$OUT" -q 320 || true
echo
echo "######## with the maps widened (mapMax 1024)"
"$OUT" -q 1024 || true
echo
echo "######## the full table, widened"
"$OUT" v 1024 | sed -n '/^-- 1:1/,/^-- inset/p'
