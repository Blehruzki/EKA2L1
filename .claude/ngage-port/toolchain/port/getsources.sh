#!/bin/bash
# getsources.sh -- fetch Symbian's published source, which SYMBIAN.md cites.
#
# These are not in this repository: they are large and they are not ours. A
# fresh container has none of them, and the answer to "how does the platform
# actually behave" is in them rather than in an emulator or in a memory. Ten
# minutes, once per container.
#
# The GitHub *API* is blocked for these repositories in this environment;
# anonymous git reads are not, so clone rather than curl the API.
set -u
ROOT="${1:-/home/user/symbiansource}"
mkdir -p "$ROOT"
for r in oss.fcl.sf.mw.classicui oss.fcl.sf.os.kernelhwsrv oss.fcl.sf.os.graphics; do
    d="$ROOT/${r##*.}"
    [ -d "$ROOT/$r/.git" ] && d="$ROOT/$r"
    if git -C "$d" rev-parse HEAD >/dev/null 2>&1; then
        echo "have   $d"
        continue
    fi
    echo "clone  $r -> $d"
    GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 \
        "https://github.com/SymbianSource/$r" "$d" || echo "FAILED $r"
done
echo
echo "What is where:"
echo "  classicui      cone/inc/COEAUI.H, uikon/coreinc/EIKAPPUI.H,"
echo "                 uifw/AvKon/src/AknAppUi.cpp, uifw/AvKon/src/aknshut.cpp"
echo "  kernelhwsrv    kernel/eka/euser/us_func.cpp and the rest of euser"
echo "  graphics       windowing/windowserver/nonnga/SERVER/Direct.CPP"
