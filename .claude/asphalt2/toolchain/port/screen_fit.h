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

enum { FIT_ONE_TO_ONE = 0, FIT_SHAPE = 1, FIT_FILL = 2, FIT_MODES = 3 };

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
    // Out.
    unsigned bufW, bufH;        // what the clear has to cover
    unsigned dstW, dstH;        // how big the picture is drawn
    unsigned offX, offY;        // and where, from the top left of the panel
    unsigned clamped;           // 1 if mapMax, not the panel, decided the size
};

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

    unsigned dw, dh;
    if (f->mode == (unsigned)FIT_ONE_TO_ONE) {
        dw = sw < bw ? sw : bw;
        dh = sh < bh ? sh : bh;
    } else if (f->mode == (unsigned)FIT_FILL) {
        // Both axes to the edges. The two scales differ, and by little enough
        // on a portrait panel to be worth the exact fit -- though not on a
        // landscape one, where E231 measured the error at +105.5%.
        dw = bw;
        dh = bh;
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
    if (dh > bh) dh = bh;
    if (dw > f->mapMax) { dw = f->mapMax; f->clamped = 1; }
    if (dh > f->mapMax) { dh = f->mapMax; f->clamped = 1; }
    f->dstW = dw;
    f->dstH = dh;

    // Which source pixel each output pixel reads: nearest neighbour, stepped
    // by accumulation, one add per output pixel.
    if (f->mode == (unsigned)FIT_ONE_TO_ONE) {
        for (unsigned i = 0; i < dw; i++) f->mapX[i] = (unsigned char)i;
        for (unsigned i = 0; i < dh; i++) f->mapY[i] = (unsigned char)i;
    } else {
        { unsigned src = 0, acc = 0;
          for (unsigned i = 0; i < dw; i++) {
              f->mapX[i] = (unsigned char)(src < sw ? src : sw - 1);
              acc += sw;
              while (acc >= dw) { acc -= dw; src++; }
          } }
        { unsigned src = 0, acc = 0;
          for (unsigned i = 0; i < dh; i++) {
              f->mapY[i] = (unsigned char)(src < sh ? src : sh - 1);
              acc += sh;
              while (acc >= dh) { acc -= dh; src++; }
          } }
    }

    f->offX = bw > dw ? (bw - dw) / 2 : 0;
    // Against the bottom, so the leftover is at the top with the status band.
    f->offY = inset + (bh > dh
        ? (f->mode == (unsigned)FIT_ONE_TO_ONE ? (bh - dh) / 2 : bh - dh)
        : 0);
}
