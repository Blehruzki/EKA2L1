#!/usr/bin/env bash
# build.sh -- build ngtest with the N-Gage SDK toolchain (GCC 2.9-psion-98r2,
# Series 60 6.1), the EKA1 two-pass link and PETRAN, as the SDK's own
# build-example.sh does. The SDK is not in this repository (it is Nokia's):
# NGAGE_SDK_ROOT names the extracted razvang-dev/Nokia-N-Gage-SDK-Toolchain.
#
#   build.sh [out dir]    -> <out>/system/apps/ngtest/ngtest.app and .rsc
set -euo pipefail
ROOT="${NGAGE_SDK_ROOT:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad/ngsdk}"
PRJ="$(cd "$(dirname "$0")" && pwd)"
OUTROOT="${1:-$PRJ/build/card}"
BUILD="$PRJ/build"; OBJ="$BUILD/obj"; OUT="$OUTROOT/system/apps/ngtest"
TOOL="$ROOT/toolchain/bin"; LIB="$ROOT/sdk/Series60/Epoc32/Release/armi/urel"
export PATH="$ROOT/toolchain/arm-epoc-pe/bin:$TOOL:$PATH"
rm -rf "$OBJ"; mkdir -p "$OBJ" "$OUT"
UID1=0x10000079; UID2=0x100039CE; UID3=0x10205E7A
CXX="$TOOL/arm-epoc-pe-g++"
"$CXX" -s -fomit-frame-pointer -O -march=armv4t -mthumb-interwork -c -nostdinc -Wall \
    -Wno-ctor-dtor-privacy -Wno-unknown-pragmas -D__DLL__ -D__SERIES60_10__ -D__SERIES60__ \
    -I"$PRJ/src" -I"$ROOT/caseinc" -I"$ROOT/sdk/Series60/Epoc32/Include" \
    "$PRJ/src/ngtest.cpp" -o "$OBJ/ngtest.o"
"$TOOL/arm-epoc-pe-ar" cr "$BUILD/ngtest.in" "$OBJ/ngtest.o"
printf 'EXPORTS\n    NewApplication__Fv @ 1 NONAME\n' > "$BUILD/ngtest.def"
NAME='ngtest[10205E7A].app'
LIBS=( "$LIB/euser.lib" "$LIB/apparc.lib" "$LIB/cone.lib" "$LIB/eikcore.lib" "$LIB/avkon.lib"
       "$LIB/ws32.lib" "$LIB/efsrv.lib" "$LIB/mediaclientaudiostream.lib" )
"$TOOL/arm-epoc-pe-dlltool" -m arm_interwork --def "$BUILD/ngtest.def" --output-exp "$BUILD/ngtest.exp" --dllname "$NAME"
"$TOOL/arm-epoc-pe-ld" -s -e _E32Dll -u _E32Dll "$BUILD/ngtest.exp" --dll --base-file "$BUILD/ngtest.bas" \
    -o "$BUILD/ngtest.pass1.app" "$LIB/edll.lib" --whole-archive "$BUILD/ngtest.in" --no-whole-archive \
    "$LIB/edllstub.lib" "$LIB/egcc.lib" "${LIBS[@]}"
"$TOOL/arm-epoc-pe-dlltool" -m arm_interwork --def "$BUILD/ngtest.def" --dllname "$NAME" \
    --base-file "$BUILD/ngtest.bas" --output-exp "$BUILD/ngtest.exp2"
"$TOOL/arm-epoc-pe-ld" -s -e _E32Dll -u _E32Dll --dll "$BUILD/ngtest.exp2" -Map "$BUILD/ngtest.map" \
    -o "$BUILD/ngtest.prepetran.app" "$LIB/edll.lib" --whole-archive "$BUILD/ngtest.in" --no-whole-archive \
    "$LIB/edllstub.lib" "$LIB/egcc.lib" "${LIBS[@]}"
"$TOOL/petran" -uid1 $UID1 -uid2 $UID2 -uid3 $UID3 -nocall "$BUILD/ngtest.prepetran.app" "$OUT/ngtest.app" >/dev/null
python3 "$PRJ/fixords.py" "$OUT/ngtest.app"
"$TOOL/cpp" -undef -nostdinc -I"$ROOT/caseinc" -I"$ROOT/sdk/Series60/Epoc32/Include" "$PRJ/res/ngtest.rss" > "$BUILD/ngtest.rpp"
"$TOOL/rcomp" -6 -u -s"$BUILD/ngtest.rpp" -o"$OUT/ngtest.rsc" >/dev/null
ls -l "$OUT"
