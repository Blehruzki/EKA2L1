#!/bin/bash
# crashrun.sh <exe> <tag> <max-secs> [delay]: a race steered into things; snapshots the framebuffer (0.5 s) and
# kills the run <delay> s after the crash camera shows (its black bars top and bottom)
SP=/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad
OLD=/tmp/claude-0/-home-user-EKA2L1/e8f9ecf8-7451-4861-beba-b7a63561cef8/scratchpad/handoff/source/asphalt4/tools
E=~/.local/share/EKA2L1/data/drives/e/sys/bin; C=~/.local/share/EKA2L1/data/drives/c/sys/hash
CROOT=~/.local/share/EKA2L1/data/drives/c
EXE="$1"; TAG="$2"; MAX="${3:-200}"; DELAY="${4:-4}"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb DISPLAY=:99 EKA2L1_FRAMESTAT=1 EKA2L1_EXCLFAULT=1
rm -rf "$SP/tsnap_$TAG"; mkdir -p "$SP/tsnap_$TAG"
export EKA2L1_MEMSNAP=D0E9C0:25800:500:$SP/tsnap_$TAG/f EKA2L1_MEMSNAP_MAX=1000 EKA2L1_MEMSNAP_PC=70000000:70100000
cp "$EXE" "$E/asphalt4_ngi.exe"; rm -rf "$SP/h_$TAG"
python3 "$OLD/make_loader_hashes.py" "$E/asphalt4_ngi.exe" "$E/ngifilemanagerfactory_2001092b.dll" "$SP/h_$TAG" >/dev/null 2>&1 || { echo HASHFAIL; exit 1; }
cp "$SP/h_$TAG/asphalt4_ngi.exe" "$C/"; cp "$SP/h_$TAG/ngifilemanagerfactory_2001092b.dll" "$C/"
rm -f "$CROOT"/a4tel*.dat
pkill -9 -x eka2l1_qt 2>/dev/null; sleep 2
pgrep -x Xvfb >/dev/null || { setsid Xvfb :99 -screen 0 1280x900x24 -noreset >/dev/null 2>&1 </dev/null & sleep 3; }
LOG="$SP/tele_$TAG.log"; :>"$LOG"
( cd /home/user/EKA2L1/build/bin && setsid ./eka2l1_qt --device RM-320 --run 'E:\sys\bin\asphalt4_ngi.exe' > "$LOG" 2>&1 </dev/null & )
for i in $(seq 1 150); do [ "$(grep -ac Framestat "$LOG")" -ge 185 ] && break; sleep 1; done
WID=$(xdotool search --name EKA2L1 | tail -1)
# MENUKEYS: the path from the boot screens into a race (default: Instant Play, the LA race)
for k in ${MENUKEYS:-Return Return Return Return Return Return}; do xdotool key --window "$WID" "$k"; sleep 3; done
xdotool keydown --window "$WID" 5; T=0; S=0; START=$(ls "$SP/tsnap_$TAG" | wc -l); crash=
while [ $T -lt $MAX ]; do
  case $((S % 4)) in 0) xdotool keydown --window "$WID" 4;; 1) xdotool keyup --window "$WID" 4;; 2) xdotool keydown --window "$WID" 6;; 3) xdotool keyup --window "$WID" 6;; esac
  S=$((S+1)); sleep 1; T=$((T+1)); [ $((T % 10)) -eq 0 ] && { xdotool keyup --window "$WID" 5; xdotool keydown --window "$WID" 5; }
  L=$(ls "$SP/tsnap_$TAG" | sort | tail -1); [ -z "$L" ] && continue
  N=$(ls "$SP/tsnap_$TAG" | wc -l); [ $N -lt $((START + 20)) ] && continue
  bars=$(python3 -c "
import struct,sys
d=open('$SP/tsnap_$TAG/$L','rb').read()
if len(d)<153600: print(255); sys.exit()
px=struct.unpack_from('<76800H',d)
def lum(y0,y1):
  s=n=0
  for y in range(y0,y1,2):
    for x in range(40,280,4):
      p=px[y*320+x]; s+=((p>>11)&31)*8+((p>>5)&63)*4+(p&31)*8; n+=1
  return s//(n*3)
print(max(lum(4,20),lum(206,216)))")
  if [ "$bars" -lt 20 ]; then echo "crash camera at ${T}s (bars $bars)"; crash=1; sleep "$DELAY"; break; fi
done
xdotool keyup --window "$WID" 4; xdotool keyup --window "$WID" 6; xdotool keyup --window "$WID" 5
pkill -9 -x eka2l1_qt 2>/dev/null; sleep 1
mkdir -p "$SP/$TAG"; rm -f "$SP/$TAG"/a4tel*.dat; cp "$CROOT"/a4tel*.dat "$SP/$TAG/" 2>/dev/null
echo "framestat=$(grep -ac Framestat "$LOG") faults=$(grep -ac 'Access violation' "$LOG") crash=${crash:-none}"
