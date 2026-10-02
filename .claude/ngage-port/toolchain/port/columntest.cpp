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
    signed char rd[512];

    // 1. No shift is the identity, whatever the line is.
    screen_columns(240, 240, 0, 0, col, rd, 512);
    bool id = true;
    for (unsigned x = 0; x < 240; x++) id &= (col[x] == x);
    check("no shift leaves every column where it was", id);

    // 2. A negative shift wraps the left edge to the right edge, and every
    //    column of the screen is still written exactly once. That last part
    //    is what build 008 broke: 14 columns went into the pad and 14
    //    columns of screen were never written at all, and showed black.
    for (int sh = -200; sh <= 200; sh += 7) {
        screen_columns(240, 240, 0, sh, col, rd, 512);
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
    screen_columns(240, 240, 0, -14, col, rd, 512);
    bool nopad = true;
    for (unsigned x = 0; x < 240; x++) nopad &= (col[x] < 240);
    check("with a 320-pixel line, nothing lands in the 80-column pad", nopad);
    check("shift -14 puts picture column 0 at screen column 226",
          col[0] == 226);
    check("shift -14 puts picture column 14 at screen column 0",
          col[14] == 0);

    // 4. A centred picture narrower than the screen still wraps inside the
    //    screen, not inside itself.
    screen_columns(240, 176, 32, 40, col, rd, 512);
    bool ok4 = true;
    for (unsigned x = 0; x < 176; x++) ok4 &= (col[x] < 240);
    check("a centred picture stays inside the screen when shifted", ok4);

    // 5. The output buffer is never overrun.
    std::memset(col, 0xAA, sizeof col);
    screen_columns(240, 4096, 0, 3, col, rd, 8);
    check("outMax is honoured", col[8] == 0xAAAA);

    // 6. **The wrap carries a row.** What the panel does is read the
    //    buffer at a linear offset, so a column pushed past an edge lands
    //    in the neighbouring row. Build 009 wrapped the column and kept
    //    the row, and round 95 photographed the result: the strip that
    //    came round the edge sat one row out. The check is that
    //    `row * screenW + column` is the linear offset the shift asks for,
    //    for every column and every shift.
    bool linear = true;
    for (int sh = -200; sh <= 200 && linear; sh += 3) {
        screen_columns(240, 240, 0, sh, col, rd, 512);
        for (unsigned x = 0; x < 240; x++) {
            const int want = (int)x + sh;                 // offset from row start
            const int got = rd[x] * 240 + (int)col[x];
            if (want != got) {
                std::printf("shift %d, column %u: linear offset %d, wanted %d\n",
                            sh, x, got, want);
                linear = false;
                break;
            }
        }
    }
    check("a wrapped column carries into the neighbouring row", linear);
    if (!linear) fails++;

    screen_columns(240, 240, 0, -22, col, rd, 512);
    check("shift -22: the first 22 columns carry one row up",
          rd[0] == -1 && rd[21] == -1 && rd[22] == 0 && rd[239] == 0);
    screen_columns(240, 240, 0, 22, col, rd, 512);
    check("shift +22: the last 22 columns carry one row down",
          rd[0] == 0 && rd[217] == 0 && rd[218] == 1 && rd[239] == 1);
    check("no shift carries no rows at all", (screen_columns(240, 240, 0, 0,
          col, rd, 512), rd[0] == 0 && rd[120] == 0 && rd[239] == 0));

    // 9. **The invariant that matters: every screen pixel written exactly
    //    once.** A shift is a rotation of the whole picture, so nothing may
    //    be lost and nothing written twice. Build 011 satisfied neither: at
    //    -22 it dropped 22 pixels of the first row and left 22 pixels of
    //    the last row's right-hand end black -- the same 22 pixels, which
    //    is what a rotation would have handed over. Testing the corner
    //    would have missed the next shift; testing the invariant does not.
    {
        const unsigned W = 240, Hh = 320;
        bool allok = true;
        for (int sh = -200; sh <= 200 && allok; sh += 11) {
            static unsigned char seen[320][240];
            std::memset(seen, 0, sizeof seen);
            screen_columns(W, W, 0, sh, col, rd, 512);
            for (unsigned y = 0; y < Hh; y++)
                for (unsigned x = 0; x < W; x++)
                    seen[screen_row(y, rd[x], Hh)][col[x]]++;
            unsigned missing = 0, twice = 0;
            for (unsigned y = 0; y < Hh; y++)
                for (unsigned x = 0; x < W; x++) {
                    if (!seen[y][x]) missing++;
                    if (seen[y][x] > 1) twice++;
                }
            if (missing || twice) {
                std::printf("shift %d: %u pixels unwritten, %u written twice\n",
                            sh, missing, twice);
                allok = false;
            }
        }
        check("every screen pixel is written exactly once, at every shift",
              allok);
        if (!allok) fails++;
    }

    std::printf("\n%s\n", fails ? "** SOME CASES FAILED **" : "all cases pass");
    return fails ? 1 : 0;
}
