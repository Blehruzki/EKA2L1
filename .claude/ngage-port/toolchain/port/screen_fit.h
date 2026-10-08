// screen_fit.h -- how a 176x208 picture is laid out on a device's panel.
//
// This is the port's scaler arithmetic, and it lives in a header of its own
// for one reason: `fittest.cpp` compiles it on the host and runs it against
// every panel that matters, in a second, with no emulator and no phone. A
// reimplementation of the same sums in the test would drift from this one and
// then agree with itself -- which is the failure this project has already paid
// for four times, most recently in phase 0 when a renderer written to match
// the blit did not match it.
//
// So there is exactly one copy of the sums. `gate6.cpp` fills a ScreenFit from
// its Context, calls screen_fit, and copies the answers back; the harness
// fills the same struct directly and checks what comes out. Nothing here
// touches Symbian, allocates, or logs, which is what makes that possible.
//
// No *variable* division: this image has no `__aeabi_uidiv` and a runtime
// divide will not link. Every ratio is done by accumulation, one add per
// output pixel. The two halvings that centre the picture are by a constant
// and compile to a shift.

// 0 to 2 keep the numbers they have always had, because a saved setting will
// carry them. Integer is added on the end.
//
//   1:1      native pixels, no resampling at all
//   aspect   as large as 176:208 allows -- the default, and within 0.6% of
//            the true ratio on every panel measured
//   fill     both axes to the edges; +7.4% distortion on an N95 and +219%
//            on an E90, kept because some people prefer the coverage
//   integer  2x, 3x... where one fits exactly: sharp like 1:1 and large
//            like aspect, but only on panels whose size happens to allow it
//   full     every pixel of the panel, status band included. The most
//            distorted of the lot and deliberately offered anyway: plenty of
//            people would rather have the whole screen than the right shape,
//            and the complaint about not being given the choice is a fair
//            one. It differs from fill in exactly one way -- fill stops at
//            the top of the status band, full does not.
enum { FIT_ONE_TO_ONE = 0, FIT_SHAPE = 1, FIT_FILL = 2, FIT_INTEGER = 3,
       FIT_FULL = 4, FIT_MODES = 5 };
// 1:1 and integer are the modes that promise exact pixels. If the source does
// not fit below the status band on some panel they do **not** crop it -- they
// centre it in the whole panel instead and let the band overlap the top. The
// alternative, which is what the port did until phase 3, is to truncate: on a
// panel exactly 176x208 that lost the bottom 56 rows, over a quarter of the
// frame and the part this game puts its HUD in. Overlapping the top of a
// picture is recoverable by switching mode; a row that was never drawn is not.
static int fit_is_exact(unsigned mode)
{
    return mode == (unsigned)FIT_ONE_TO_ONE || mode == (unsigned)FIT_INTEGER;
}

struct ScreenFit {
    // In: the panel, the rows at the top to stay clear of, the source, the mode.
    unsigned screenW, screenH;
    unsigned topInset;
    unsigned srcW, srcH;
    unsigned mode;
    // In: where the tables go and how many entries there is room for. The
    // entries are source indices, so a byte each is enough while the source
    // stays under 256 pixels; mapMax is a count of destination pixels, and it
    // is the ceiling on how large the picture may be drawn.
    unsigned char *mapX, *mapY;
    unsigned mapMax;
    // In, optional (null: not wanted): per destination pixel, how much of it
    // lies in the *next* source pixel, 0..255 of 256 -- the blend weight of
    // the area-weighted filter (round 150). 0 means the pixel is copied; it
    // is 0 everywhere in an exact mode, so those stay pixel-sharp.
    unsigned char *wX, *wY;
    // Out.
    unsigned bufW, bufH;        // what the clear has to cover
    unsigned dstW, dstH;        // how big the picture is drawn
    unsigned offX, offY;        // and where, from the top left of the panel
    unsigned clamped;           // 1 if mapMax, not the panel, decided the size
};

// Unsigned division by shift and subtract: the image has no `__aeabi_uidiv`,
// and this runs once per layout over at most mapMax entries, not per pixel.
static unsigned fit_udiv(unsigned a, unsigned b)
{
    unsigned q = 0, r = 0;
    if (!b) return 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) { r -= b; q |= 1u << i; }
    }
    return q;
}

static void screen_fit(ScreenFit *f)
{
    const unsigned bw = f->screenW;
    const unsigned inset = (f->topInset < f->screenH) ? f->topInset : 0u;
    const unsigned bh = f->screenH - inset;
    const unsigned sw = f->srcW;
    const unsigned sh = f->srcH;

    f->bufW = bw;
    f->bufH = f->screenH;       // the clear still covers the whole panel
    f->clamped = 0;

    if (!bw || !bh || !sw || !sh) {
        f->dstW = sw;
        f->dstH = sh;
        f->offX = f->offY = 0;
        for (unsigned i = 0; i < sw && i < f->mapMax; i++) f->mapX[i] = (unsigned char)i;
        for (unsigned i = 0; i < sh && i < f->mapMax; i++) f->mapY[i] = (unsigned char)i;
        return;
    }

    // An exact mode that cannot fit below the band uses the whole panel
    // rather than losing rows off the bottom. Everything downstream -- the
    // centring, the bounds -- then works against that taller area.
    unsigned useH = bh, useTop = inset;
    if (fit_is_exact(f->mode) && bh < sh && f->screenH >= sh) {
        useH = f->screenH;
        useTop = 0;
    }

    // Full screen ignores the band outright: that is the whole of its
    // purpose, so it is decided before anything else looks at the inset.
    if (f->mode == (unsigned)FIT_FULL) {
        useH = f->screenH;
        useTop = 0;
    }

    unsigned dw, dh;
    unsigned scale = 1;                 // how many output pixels per source one
    if (f->mode == (unsigned)FIT_ONE_TO_ONE) {
        dw = sw < bw ? sw : bw;
        dh = sh < useH ? sh : useH;
    } else if (f->mode == (unsigned)FIT_INTEGER) {
        // The largest whole multiple that fits. One is always allowed, so
        // this degrades to 1:1 on a panel too small to double.
        unsigned n = 1;
        while ((n + 1) * sw <= bw && (n + 1) * sh <= useH)
            n++;
        scale = n;
        dw = n * sw;
        dh = n * sh;
        if (dw > bw) { dw = sw < bw ? sw : bw; scale = 1; }   // cannot fit once
        if (dh > useH) { dh = sh < useH ? sh : useH; scale = 1; }
    } else if (f->mode == (unsigned)FIT_FILL || f->mode == (unsigned)FIT_FULL) {
        // Both axes to the edges of whatever area the mode is using. The two
        // scales differ, and by little enough on a portrait panel to be worth
        // the exact fit -- though not on a landscape one, where E231 measured
        // the error at +105.5%.
        dw = bw;
        dh = useH;
    } else if (bw * sh > bh * sw) {         // shape kept, height-limited
        dh = bh;
        dw = 0;
        for (unsigned i = 0, acc = 0; i < dh; i++) {
            acc += sw;
            while (acc >= sh) { acc -= sh; dw++; }
        }
    } else {                                // shape kept, width-limited
        dw = bw;
        dh = 0;
        for (unsigned i = 0, acc = 0; i < dw; i++) {
            acc += sh;
            while (acc >= sw) { acc -= sw; dh++; }
        }
    }

    if (dw > bw) dw = bw;
    if (dh > useH) dh = useH;
    if (dw > f->mapMax) { dw = f->mapMax; f->clamped = 1; }
    if (dh > f->mapMax) { dh = f->mapMax; f->clamped = 1; }
    f->dstW = dw;
    f->dstH = dh;

    // Which source pixel each output pixel reads: nearest neighbour, stepped
    // by accumulation, one add per output pixel.
    if (fit_is_exact(f->mode)) {
        // 1:1 is the identity; an n-times integer scale repeats each source
        // pixel n times. Counted, not divided -- `scale` is already known and
        // a variable divide would want `__aeabi_uidiv`, which this image does
        // not have.
        { unsigned src = 0, k = 0;
          for (unsigned i = 0; i < dw; i++) {
              f->mapX[i] = (unsigned char)(src < sw ? src : sw - 1);
              if (f->wX) f->wX[i] = 0;
              if (++k == scale) { k = 0; src++; }
          } }
        { unsigned src = 0, k = 0;
          for (unsigned i = 0; i < dh; i++) {
              f->mapY[i] = (unsigned char)(src < sh ? src : sh - 1);
              if (f->wY) f->wY[i] = 0;
              if (++k == scale) { k = 0; src++; }
          } }
    } else {
        // The weight falls out of the same accumulation: after the step for
        // pixel i, `acc` is (i+1)*sw - src*dw, the part of destination pixel
        // i (sw units long) that lies in source pixel `src`. A boundary that
        // fell inside the pixel moved `src` on by exactly one; one that fell
        // on its edge leaves acc 0; a step of two or more is a downscale,
        // which this filter does not do, so nearest there (a host check
        // against the exact area formula agrees to within one on every
        // upscaling panel; the one disagreement is a downscaled landscape
        // column whose end lands exactly on a source edge).
        { unsigned src = 0, acc = 0;
          for (unsigned i = 0; i < dw; i++) {
              f->mapX[i] = (unsigned char)(src < sw ? src : sw - 1);
              const unsigned before = src;
              acc += sw;
              while (acc >= dw) { acc -= dw; src++; }
              if (f->wX) {
                  unsigned w = (src == before + 1 && i + 1 < dw && src < sw && acc < sw)
                             ? fit_udiv(acc * 256u, sw) : 0u;
                  f->wX[i] = (unsigned char)(w > 255u ? 255u : w);
              }
          } }
        { unsigned src = 0, acc = 0;
          for (unsigned i = 0; i < dh; i++) {
              f->mapY[i] = (unsigned char)(src < sh ? src : sh - 1);
              const unsigned before = src;
              acc += sh;
              while (acc >= dh) { acc -= dh; src++; }
              if (f->wY) {
                  unsigned w = (src == before + 1 && i + 1 < dh && src < sh && acc < sh)
                             ? fit_udiv(acc * 256u, sh) : 0u;
                  f->wY[i] = (unsigned char)(w > 255u ? 255u : w);
              }
          } }
    }

    f->offX = bw > dw ? (bw - dw) / 2 : 0;
    // Centred in whatever area the mode is using. The port bottom-anchored
    // the scaled modes until phase 3, which was chosen on an N95 where those
    // modes leave no vertical slack at all -- so it never showed there, and
    // on a tall panel like a 5800 it dropped the picture 160 rows below the
    // band for no reason. Centring is a no-op on every 240x320 layout and
    // better on the rest; the harness checks that claim.
    f->offY = useTop + (useH > dh ? (useH - dh) / 2 : 0);
}

// **Where each column of the picture lands in the frame buffer.**
//
// Here rather than in gate6.cpp so that a host test can run exactly this,
// which the bench cannot: EKA2L1's frame buffer line is the screen width
// and the N95's is 320 pixels against a visible 240. Build 008 wrapped the
// shift modulo the *line*, which on the bench is the same number and is
// correct, and on the phone pushed the picture into the 80-pixel pad and
// left black behind it. One machine could not see the bug the other had.
//
// **And a wrap carries a row.** What the panel does is read the buffer at a
// linear offset, so picture pixel (x, y) has to be written at linear
// offset `y * screenW + x + shift`. When `x + shift` leaves the row that
// offset is in the *neighbouring* row, not the same one:
//
//     x + shift >= screenW   ->  row y + 1, column x + shift - screenW
//     x + shift <  0         ->  row y - 1, column x + shift + screenW
//
// Build 009 wrapped the column and kept the row, so the strip that came
// round the edge sat one row out -- which is what round 95 photographed
// once the alignment was otherwise right. `rowd[i]` is that carry, -1, 0
// or +1, and the caller has to skip a pixel whose carried row falls off
// the buffer.
//
// `screenW` is the visible width, `dstW` the picture's, `offX` where it
// starts, `shift` the keypad nudge in columns. No divide: `shift` is
// bounded well inside one screen width, so one add or one subtract always
// brings it back.
static inline void screen_columns(unsigned screenW, unsigned dstW,
                                  unsigned offX, int shift,
                                  unsigned short *out, signed char *rowd,
                                  unsigned outMax)
{
    const int w = (int)(screenW ? screenW : 1u);
    for (unsigned x = 0; x < dstW && x < outMax; x++) {
        int col = (int)(offX + x) + shift;
        int dy = 0;
        if (col < 0)        { col += w; dy = -1; }
        else if (col >= w)  { col -= w; dy = +1; }
        if (col < 0 || col >= w) { col = (int)(offX + x); dy = 0; }
        out[x] = (unsigned short)col;
        if (rowd)
            rowd[x] = (signed char)dy;
    }
}

// **The carried row wraps too, and that is not a detail.**
//
// What the shift is correcting is a scan-out that starts a fixed number of
// pixels into the buffer, so the whole picture is one rotation of a flat
// array of screenW * dstH pixels -- not a per-row slide. Under a rotation
// nothing is lost: the pixels that fall off one end arrive at the other.
//
// Build 011 carried the row and then *skipped* a carry that left the
// picture, which is not the same thing: at a shift of -22 it drops the 22
// pixels of picture row 0 that belong off the top, and leaves the 22
// screen pixels of the last row's right-hand end with nothing written into
// them at all -- a black notch in a corner. Those are the same 22 pixels.
// Wrapping the row hands them over and the rotation is complete.
//
// `y` is the picture row, and the answer is the picture row whose pixels
// belong there. The caller adds its own vertical offset.
static inline unsigned screen_row(unsigned y, int dy, unsigned dstH)
{
    if (!dstH)
        return y;
    int ry = (int)y + dy;
    if (ry < 0)
        ry += (int)dstH;
    else if (ry >= (int)dstH)
        ry -= (int)dstH;
    return (unsigned)ry;
}
