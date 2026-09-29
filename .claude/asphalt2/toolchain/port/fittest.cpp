// fittest.cpp -- run the port's own scaler arithmetic over every panel that
// matters, on the host, in a second.
//
//     c++ -O1 -Wall -o fittest fittest.cpp && ./fittest
//
// It includes `screen_fit.h`, which is the same header `gate6.cpp` includes,
// so what is checked here is what ships. That is the whole point: the bench
// has one S60v3 ROM and the emulator override only reaches panels the ROM has
// Avkon layouts for (E231-E233), so every other resolution can only be
// covered by arithmetic -- and arithmetic checked against a second copy of
// itself proves nothing.
//
// Exit status is the number of failures, so it can gate a commit.

#include <cstdio>
#include <cstring>
#include <cstdlib>

typedef unsigned u32;
#include "screen_fit.h"

// Room for the largest panel worth considering, so the harness can tell a
// real clamp from one it imposed itself.
enum { MAPMAX = 1024 };
static unsigned char mapX[MAPMAX], mapY[MAPMAX];

struct Panel { const char *name; unsigned w, h; };

static const Panel kPanels[] = {
    { "N95 / C5-00 / N79",      240, 320 },
    { "E71 landscape",          320, 240 },
    { "N80 / N90",              352, 416 },
    { "5800 / N97",             360, 640 },
    { "5800 landscape",         640, 360 },
    { "N91 / 6110",             176, 220 },
    { "smaller than source",    128, 160 },
    { "E90 inner",              800, 352 },
    { "square oddity",          320, 320 },
    { "exactly the source",     176, 208 },
};

static const char *kModeName[FIT_MODES] = { "1:1", "aspect", "fill" };

// Failures are collapsed by (panel, mode, reason, whether the clamp was in
// force), because the inset sweep runs the same case fifty times and a wall
// of identical lines hides how many distinct things are wrong. One line each,
// with a count and the first example.
struct Failure {
    const char *panel; unsigned mode; const char *what; unsigned clamped;
    unsigned count;
    unsigned w, h, dw, dh, ox, oy, inset;
};
enum { MAX_FAILURES = 64 };
static Failure fails[MAX_FAILURES];
static int nfail;
static int total;

static void check(bool ok, const char *what, const Panel &p, unsigned mode,
                  const ScreenFit &f)
{
    if (ok)
        return;
    total++;
    for (int i = 0; i < nfail; i++)
        if (fails[i].panel == p.name && fails[i].mode == mode
            && fails[i].what == what && fails[i].clamped == f.clamped) {
            fails[i].count++;
            return;
        }
    if (nfail >= MAX_FAILURES)
        return;
    Failure &r = fails[nfail++];
    r.panel = p.name; r.mode = mode; r.what = what; r.clamped = f.clamped;
    r.count = 1;
    r.w = f.screenW; r.h = f.screenH; r.dw = f.dstW; r.dh = f.dstH;
    r.ox = f.offX; r.oy = f.offY; r.inset = f.topInset;
}

static void report(void)
{
    if (!nfail) {
        std::printf("no failures\n");
        return;
    }
    std::printf("%d distinct failure(s), %d case(s) in all:\n", nfail, total);
    for (int i = 0; i < nfail; i++) {
        const Failure &r = fails[i];
        std::printf("  %-20s %4ux%-4u %-6s  %s%s\n"
                    "      x%-4u  first: %ux%u at (%u,%u), inset %u\n",
                    r.panel, r.w, r.h, kModeName[r.mode], r.what,
                    r.clamped ? "  [because the map clamped]" : "",
                    r.count, r.dw, r.dh, r.ox, r.oy, r.inset);
    }
}

static void run(const Panel &p, unsigned mode, unsigned inset, unsigned mapMax,
                bool verbose)
{
    ScreenFit f;
    std::memset(&f, 0, sizeof f);
    std::memset(mapX, 0xAA, sizeof mapX);
    std::memset(mapY, 0xAA, sizeof mapY);
    f.screenW = p.w; f.screenH = p.h; f.topInset = inset;
    f.srcW = 176; f.srcH = 208; f.mode = mode;
    f.mapX = mapX; f.mapY = mapY; f.mapMax = mapMax;
    screen_fit(&f);

    const unsigned appliedInset = (inset < p.h) ? inset : 0u;

    // 1. The picture is on the panel. Nothing else matters if this fails:
    //    the blit writes dstW x dstH pixels at (offX, offY) into the real
    //    framebuffer, so past the edge is memory that is not ours.
    check(f.offX + f.dstW <= p.w, "picture runs off the right edge", p, mode, f);
    check(f.offY + f.dstH <= p.h, "picture runs off the bottom", p, mode, f);

    // 2. It clears the status band it was told to.
    check(f.offY >= appliedInset || f.dstH == 0, "picture starts above the inset",
          p, mode, f);

    // 3. Every map entry addresses a real source pixel. An entry past the
    //    source reads another row; an entry past 255 cannot be stored at all.
    for (unsigned i = 0; i < f.dstW; i++)
        if (mapX[i] >= f.srcW) {
            check(false, "mapX addresses past the source width", p, mode, f);
            break;
        }
    for (unsigned i = 0; i < f.dstH; i++)
        if (mapY[i] >= f.srcH) {
            check(false, "mapY addresses past the source height", p, mode, f);
            break;
        }

    // 4. Nothing was written past what the caller offered.
    bool spill = false;
    for (unsigned i = f.dstW; i < mapMax; i++) if (mapX[i] != 0xAA) spill = true;
    for (unsigned i = f.dstH; i < mapMax; i++) if (mapY[i] != 0xAA) spill = true;
    check(!spill, "wrote map entries past the picture", p, mode, f);

    // 5. The maps rise: a scaler that goes backwards mirrors part of the image.
    for (unsigned i = 1; i < f.dstW; i++)
        if (mapX[i] < mapX[i - 1]) { check(false, "mapX is not monotonic", p, mode, f); break; }
    for (unsigned i = 1; i < f.dstH; i++)
        if (mapY[i] < mapY[i - 1]) { check(false, "mapY is not monotonic", p, mode, f); break; }

    // 6. Mode-specific promises.
    if (mode == FIT_ONE_TO_ONE) {
        for (unsigned i = 0; i < f.dstW; i++)
            if (mapX[i] != i) { check(false, "1:1 does not map pixel for pixel", p, mode, f); break; }
        // 1:1 means all of it, not just the part that fits below the band.
        // `dstH = sh < bh ? sh : bh` truncates, so on any panel whose usable
        // height is under 208 the bottom of the frame is simply not drawn --
        // and the bottom of this game's frame is where the HUD lives. Only
        // counted when the panel itself is tall enough and the inset is what
        // did the cropping; a panel genuinely shorter than the source is
        // physics, not a defect.
        check(f.dstH == f.srcH || p.h < f.srcH,
              "1:1 crops the source: the inset, not the panel, is too small",
              p, mode, f);
        check(f.dstW == f.srcW || p.w < f.srcW,
              "1:1 crops the source horizontally", p, mode, f);
    }
    if (mode == FIT_SHAPE && f.dstW && f.dstH) {
        // Keeping the shape means the ratio survives. One output pixel of
        // slack each way, which is what integer accumulation costs.
        const long lhs = (long)f.dstW * 208, rhs = (long)f.dstH * 176;
        const long slack = 208L + 176L;
        check(lhs - rhs <= slack && rhs - lhs <= slack, "aspect not kept", p, mode, f);
    }
    if (mode == FIT_FILL && !f.clamped) {
        check(f.dstW == p.w, "fill does not reach the side edges", p, mode, f);
        check(f.dstH == p.h - appliedInset, "fill does not reach the bottom", p, mode, f);
    }

    if (verbose) {
        double aspect = f.dstH ? (double)f.dstW / f.dstH : 0.0;
        double err = 100.0 * (aspect / (176.0 / 208.0) - 1.0);
        double used = 100.0 * (double)f.dstW * f.dstH / ((double)p.w * p.h);
        std::printf("  %-20s %4ux%-4u %-6s -> %3ux%-3u at (%3u,%3u)  "
                    "aspect %+6.1f%%  screen %4.1f%%%s\n",
                    p.name, p.w, p.h, kModeName[mode],
                    f.dstW, f.dstH, f.offX, f.offY, err, used,
                    f.clamped ? "  CLAMPED" : "");
    }
}

int main(int argc, char **argv)
{
    const bool verbose = (argc > 1 && std::strcmp(argv[1], "-q") != 0);
    const unsigned mapMax = (argc > 2) ? (unsigned)atoi(argv[2]) : 320u;

    std::printf("scaler over %zu panels x %d modes, mapMax %u, source 176x208\n\n",
                sizeof kPanels / sizeof kPanels[0], FIT_MODES, mapMax);

    for (unsigned mode = 0; mode < FIT_MODES; mode++) {
        if (verbose) std::printf("-- %s\n", kModeName[mode]);
        for (const Panel &p : kPanels)
            run(p, mode, 56, mapMax, verbose);
        if (verbose) std::printf("\n");
    }

    // The inset is a per-device number the port currently hardcodes, so sweep
    // it: 0 through taller than the panel, which must not fault or wrap.
    std::printf("-- inset sweep 0..400 in steps of 8, all panels, all modes\n");
    for (unsigned mode = 0; mode < FIT_MODES; mode++)
        for (const Panel &p : kPanels)
            for (unsigned inset = 0; inset <= 400; inset += 8)
                run(p, mode, inset, mapMax, false);

    std::printf("\n");
    report();
    return nfail;
}
