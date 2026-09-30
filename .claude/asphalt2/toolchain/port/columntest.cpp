// columntest.cpp -- the keypad shift, in the geometry the bench cannot have.
//
// EKA2L1's frame buffer line is exactly the screen width. Every phone's is
// wider: the N95 keeps a 320-pixel line for a 240-pixel screen. Build 008
// wrapped the shift modulo the line, which is right on the bench and wrong
// on the phone -- it pushed the picture into the 80 columns the panel never
// shows and left black where it had been. Round 94 reported exactly that.
//
// So the arithmetic is tested here, on the host, against both geometries.
//
//     c++ -O2 -o /tmp/columntest columntest.cpp && /tmp/columntest
#include <cstdio>
#include <cstring>
#include "screen_fit.h"

static int fails = 0;

static void check(const char *what, bool ok)
{
    std::printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) fails++;
}

int main()
{
    unsigned short col[512];

    // 1. No shift is the identity, whatever the line is.
    screen_columns(240, 240, 0, 0, col, 512);
    bool id = true;
    for (unsigned x = 0; x < 240; x++) id &= (col[x] == x);
    check("no shift leaves every column where it was", id);

    // 2. A negative shift wraps the left edge to the right edge, and every
    //    column of the screen is still written exactly once. That last part
    //    is what build 008 broke: 14 columns went into the pad and 14
    //    columns of screen were never written at all, and showed black.
    for (int sh = -200; sh <= 200; sh += 7) {
        screen_columns(240, 240, 0, sh, col, 512);
        int seen[240];
        std::memset(seen, 0, sizeof seen);
        bool inrange = true;
        for (unsigned x = 0; x < 240; x++) {
            if (col[x] >= 240) { inrange = false; break; }
            seen[col[x]]++;
        }
        bool once = inrange;
        for (int i = 0; i < 240 && once; i++) once = (seen[i] == 1);
        if (!once) {
            std::printf("shift %d: columns not a permutation of the screen\n", sh);
            fails++;
            break;
        }
    }
    check("every shift is a permutation of the visible columns", fails == 0);

    // 3. The specific case from the phone: 240 visible, a 320-pixel line,
    //    shift -14. Nothing may land in the pad.
    screen_columns(240, 240, 0, -14, col, 512);
    bool nopad = true;
    for (unsigned x = 0; x < 240; x++) nopad &= (col[x] < 240);
    check("with a 320-pixel line, nothing lands in the 80-column pad", nopad);
    check("shift -14 puts picture column 0 at screen column 226",
          col[0] == 226);
    check("shift -14 puts picture column 14 at screen column 0",
          col[14] == 0);

    // 4. A centred picture narrower than the screen still wraps inside the
    //    screen, not inside itself.
    screen_columns(240, 176, 32, 40, col, 512);
    bool ok4 = true;
    for (unsigned x = 0; x < 176; x++) ok4 &= (col[x] < 240);
    check("a centred picture stays inside the screen when shifted", ok4);

    // 5. The output buffer is never overrun.
    std::memset(col, 0xAA, sizeof col);
    screen_columns(240, 4096, 0, 3, col, 8);
    check("outMax is honoured", col[8] == 0xAAAA);

    std::printf("\n%s\n", fails ? "** SOME CASES FAILED **" : "all cases pass");
    return fails ? 1 : 0;
}
