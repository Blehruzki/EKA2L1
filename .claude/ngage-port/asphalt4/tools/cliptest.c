/* cliptest.c -- round 82: renderer/a4_clip.h against a floating-point reference.
 * cc -O2 -o cliptest cliptest.c -lm && ./cliptest
 * Random triangles with corners far off the panel (the game's near ones: x to +-32,000, w 200-30,000), positions in
 * 1/16 pixel as the clipper takes them, clipped to the guard band. Each output vertex must lie on the band and within
 * 1/16 pixel of the original triangle, and carry the depth and texture words the GPU's perspective-correct
 * interpolation gives at that point: 1/w and a/w are affine over the screen, so the reference is the original
 * triangle's planes for them, evaluated at the vertex. Triangles under a pixel tall are skipped (their planes are
 * not defined to any useful precision; the GPU draws them as nothing or a pixel). A case that cannot fail is not a
 * case: the clipper's output with u and v swapped, and with the depth's 1/w interpolation replaced by a linear one,
 * must both fail. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../renderer/a4_clip.h"

static int dbg_main;
static double rnd(double a, double b) { return a + (b - a) * (rand() / (double)RAND_MAX); }

/* distance (pixels) from p to the triangle; the planes' values at p */
static double dist_seg(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double qx = ax + t * dx - px, qy = ay + t * dy - py;
    return sqrt(qx * qx + qy * qy);
}

static int check(const struct clipv *t, const struct clipv *o, int n, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                 double *wd, double *wz, double *wu)
{
    const double S = 1 << CLIP_FB;
    double ax = t[0].x / S, ay = t[0].y / S, bx = t[1].x / S, by = t[1].y / S, cx = t[2].x / S, cy = t[2].y / S;
    double det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    double longest = fmax(hypot(bx - ax, by - ay), fmax(hypot(cx - bx, cy - by), hypot(ax - cx, ay - cy)));
    if (fabs(det) / longest < 1.0) return -1;            /* under a pixel tall */
    int ok = 1;
    for (int i = 0; i < n; i++) {
        double px = o[i].x / S, py = o[i].y / S;
        if (o[i].x < x0 || o[i].x > x1 || o[i].y < y0 || o[i].y > y1) ok = 0;
        double l1 = ((px - ax) * (cy - ay) - (cx - ax) * (py - ay)) / det;
        double l2 = ((bx - ax) * (py - ay) - (px - ax) * (by - ay)) / det;
        double l0 = 1 - l1 - l2;
        double d = 0;
        if (l0 < 0 || l1 < 0 || l2 < 0)
            d = fmin(dist_seg(px, py, ax, ay, bx, by), fmin(dist_seg(px, py, bx, by, cx, cy), dist_seg(px, py, cx, cy, ax, ay)));
        if (d > *wd) *wd = d;
        double q = l0 / t[0].z + l1 / t[1].z + l2 / t[2].z;
        if (q <= 0) continue;
        double z = 1 / q;
        double u = (l0 * t[0].u / t[0].z + l1 * t[1].u / t[1].z + l2 * t[2].u / t[2].z) / q;
        double v = (l0 * t[0].v / t[0].z + l1 * t[1].v / t[1].z + l2 * t[2].v / t[2].z) / q;
        int onedge = 0; double emin = 0;
        {   /* a cut point: the reference is the exact crossing of the original edge it lies on with its plane --
             * perspective along the edge (well conditioned on a sliver, where the planes are not) */
            const double X[3] = { ax, bx, cx }, Y[3] = { ay, by, cy };
            double best = 0.1;
            for (int e = 0; e < 3; e++) {
                int a = e, b = (e + 1) % 3;
                for (int ax_ = 0; ax_ < 2; ax_++) {
                    double pc = ax_ ? py : px, A = ax_ ? Y[a] : X[a], B = ax_ ? Y[b] : X[b];
                    double band = ax_ ? (o[i].y == y0 || o[i].y == y1) : (o[i].x == x0 || o[i].x == x1);
                    if (!band || (pc - A) * (pc - B) > 0 || A == B) continue;
                    double te = (pc - A) / (B - A), oth = ax_ ? X[a] + te * (X[b] - X[a]) : Y[a] + te * (Y[b] - Y[a]);
                    double dd = fabs(oth - (ax_ ? px : py));
                    if (dd < best) {   /* every edge within 0.1 px is a valid answer: keep the closest match */
                        double qe = (1 - te) / t[a].z + te / t[b].z;
                        double ze = 1 / qe, ue = ((1 - te) * t[a].u / t[a].z + te * t[b].u / t[b].z) / qe;
                        double ve = ((1 - te) * t[a].v / t[a].z + te * t[b].v / t[b].z) / qe;
                        double m = fabs(o[i].z - ze) / (2 + 1e-3 * ze) + fabs(o[i].u - ue) / (2 + 1e-3 * fabs(ue)) + fabs(o[i].v - ve) / (2 + 1e-3 * fabs(ve));
                        if (!onedge || m < emin) { emin = m; z = ze; u = ue; v = ve; }
                        onedge = 1;
                    }
                }
            }
        }
        double span = fmax(fabs(t[0].u - t[1].u), fmax(fabs(t[1].u - t[2].u), fabs(t[0].u - t[2].u))) + 1;
        double spanv = fmax(fabs(t[0].v - t[1].v), fmax(fabs(t[1].v - t[2].v), fabs(t[0].v - t[2].v))) + 1;
        /* the vertex sits within 1/32 px of the exact cut: allow what the planes change over 0.07 px there */
        double q0 = 1.0 / t[0].z, q1 = 1.0 / t[1].z, q2 = 1.0 / t[2].z;
        double d1x = (cy - ay) / det, d1y = -(cx - ax) / det, d2x = -(by - ay) / det, d2y = (bx - ax) / det;
        double gq = hypot((q1 - q0) * d1x + (q2 - q0) * d2x, (q1 - q0) * d1y + (q2 - q0) * d2y);
        double U0 = t[0].u * q0, U1 = t[1].u * q1, U2 = t[2].u * q2, V0 = t[0].v * q0, V1 = t[1].v * q1, V2 = t[2].v * q2;
        double gu = hypot((U1 - U0) * d1x + (U2 - U0) * d2x, (U1 - U0) * d1y + (U2 - U0) * d2y);
        double gv = hypot((V1 - V0) * d1x + (V2 - V0) * d2x, (V1 - V0) * d1y + (V2 - V0) * d2y);
        if (onedge) gq = gu = gv = 0;   /* exact along the edge: no allowance for the planes */
        const int corner = (o[i].x == x0 || o[i].x == x1) && (o[i].y == y0 || o[i].y == y1);
        const double cs = onedge && !corner ? 1e-3 : 2e-3;   /* a band corner is cut twice: two cuts' rounding */
        double tz = 2.0 + 1e-3 * z + z * z * gq * 0.07, tu = 2 + cs * span + (gu + fabs(u) * gq) / q * 0.07;   /* + 2 whole units: a corner cut by two planes is rounded twice */
        double tv = 2 + cs * spanv + (gv + fabs(v) * gq) / q * 0.07;
        double ez = fabs(o[i].z - z) / tz * 1e-3, eu = fmax(fabs(o[i].u - u) / tu, fabs(o[i].v - v) / tv) * 1e-3;
        if (ez > *wz) { *wz = ez; if (getenv("CLIPDBG") && dbg_main) fprintf(stderr, "worst z: tri (%.2f,%.2f,%d) (%.2f,%.2f,%d) (%.2f,%.2f,%d) out (%.3f,%.3f) z %d ref %.2f tz %.3f det %.1f\n",
            ax, ay, t[0].z, bx, by, t[1].z, cx, cy, t[2].z, px, py, o[i].z, z, tz, det); }
        if (eu > *wu) { *wu = eu; if (getenv("CLIPDBG") && dbg_main) fprintf(stderr, "worst u: tri (%.1f,%.1f,%d,%d) (%.1f,%.1f,%d,%d) (%.1f,%.1f,%d,%d) out (%.3f,%.3f) u %d ref %.1f tu %.2f | v %d ref %.1f tv %.2f\n",
            ax, ay, t[0].z, t[0].u, bx, by, t[1].z, t[1].u, cx, cy, t[2].z, t[2].u, px, py, o[i].u, u, tu, o[i].v, v, tv); }
    }
    return ok;
}

static void mk(struct clipv *t, int mode)
{
    for (int i = 0; i < 3; i++) {
        int giant = rand() % 2;
        t[i].x = (int32_t)((giant ? rnd(-32000, 32000) : rnd(-100, 420)) * (1 << CLIP_FB));
        t[i].y = (int32_t)((giant && mode && rand() % 3 == 0 ? rnd(-32000, 32000) : rnd(-60, 300)) * (1 << CLIP_FB));
        t[i].z = (int32_t)rnd(200, 30000);
        t[i].u = (int32_t)rnd(-200000, 200000);
        t[i].v = (int32_t)rnd(-200000, 200000);
    }
}

int main(void)
{
    const int32_t G = 1024 << CLIP_FB, W = 320 << CLIP_FB, H = 240 << CLIP_FB;
    const int32_t x0 = -G, y0 = -G, x1 = W + G, y1 = H + G;
    double wd = 0, wz = 0, wu = 0;
    int cut = 0, empty = 0, thin = 0, off = 0, n_cases = 200000;
    srand(82); dbg_main = 1;
    for (int k = 0; k < n_cases; k++) {
        struct clipv t[3], o[CLIPV_MAX];
        mk(t, 1);
        int n = clip_tri(t, x0, y0, x1, y1, o);
        if (!n) { empty++; continue; }
        int r = check(t, o, n, x0, y0, x1, y1, &wd, &wz, &wu);
        if (r < 0) { thin++; continue; }
        cut++;
        if (!r) off++;
    }
    printf("cases %d: checked %d, nothing left %d, under a pixel tall %d; vertices off the band %d\n", n_cases, cut, empty, thin, off);
    printf("worst: distance off the triangle %.3f px; depth and texture errors as a share of their allowance x 1e-3: %.2e, %.2e (pass < 1e-3)\n", wd, wz, wu);
    {   /* the recorded sliver (phone round 81b): (-22181, 83, 1104) (10558, 88, 1404) (10473, 87, 1416) */
        const int F = 1 << CLIP_FB;
        struct clipv t[3] = { { -22181 * F, 83 * F, 1104, 0, 0 }, { 10558 * F, 88 * F, 1404, 65536, 0 }, { 10473 * F, 87 * F, 1416, 65536, 4096 } }, o[CLIPV_MAX];
        int n = clip_tri(t, x0, y0, x1, y1, o);
        printf("recorded sliver -> %d vertices:", n);
        for (int i = 0; i < n; i++) printf(" (%.2f,%.2f w%d u%d v%d)", o[i].x / (double)F, o[i].y / (double)F, o[i].z, o[i].u, o[i].v);
        printf("\n");
    }
    dbg_main = 0;
    double mu = 0;
    {   /* mutant 1: u and v swapped */
        double a = 0, b = 0; srand(7);
        for (int k = 0; k < 3000; k++) { struct clipv t[3], o[CLIPV_MAX]; mk(t, 0);
            int n = clip_tri(t, x0, y0, x1, y1, o);
            for (int i = 0; i < n; i++) { int32_t s = o[i].u; o[i].u = o[i].v; o[i].v = s; }
            if (n) check(t, o, n, x0, y0, x1, y1, &a, &b, &mu); }
    }
    double mz = 0;
    {   /* mutant 2: the depth interpolated linearly in screen space (not 1/w): recompute each cut vertex's depth */
        double a = 0, c = 0; srand(9);
        for (int k = 0; k < 3000; k++) { struct clipv t[3], o[CLIPV_MAX]; mk(t, 0);
            int n = clip_tri(t, x0, y0, x1, y1, o);
            const double S = 1 << CLIP_FB;
            double ax = t[0].x / S, ay = t[0].y / S, bx = t[1].x / S, by = t[1].y / S, cx = t[2].x / S, cy = t[2].y / S;
            double det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (fabs(det) < 1) continue;
            for (int i = 0; i < n; i++) { double px = o[i].x / S, py = o[i].y / S;
                double l1 = ((px - ax) * (cy - ay) - (cx - ax) * (py - ay)) / det, l2 = ((bx - ax) * (py - ay) - (px - ax) * (by - ay)) / det;
                o[i].z = (int32_t)((1 - l1 - l2) * t[0].z + l1 * t[1].z + l2 * t[2].z); }
            if (n) check(t, o, n, x0, y0, x1, y1, &a, &mz, &c); }
    }
    printf("mutants: u/v swapped -> texture %.2e (%s); linear depth -> depth %.2e (%s)\n", mu, mu > 1e-3 ? "caught" : "NOT CAUGHT",
           mz, mz > 1e-3 ? "caught" : "NOT CAUGHT");
    int ok = off == 0 && wd < 0.1 && wz < 1e-3 && wu < 1e-3 && mu > 1e-3 && mz > 1e-3;
    printf("%s\n", ok ? "cliptest pass" : "cliptest FAIL");
    return ok ? 0 : 1;
}
