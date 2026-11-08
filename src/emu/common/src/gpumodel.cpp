/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <common/gpumodel.h>
#include <common/time.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace eka2l1::common::gpumodel {
    static params g_params;
    static bool g_on = false;
    static std::uint64_t g_tris = 0;      // triangles submitted since the last read of the frame
    static bool g_dirty = false;          // drawn to since the last read: the tile render is still owed
    static double g_owed_us = 0;          // fractions of a cycle carried

    // what the model charged, for calibrating it: a line every ~5 s of host time
    struct counters {
        std::uint64_t calls = 0, draws = 0, verts = 0, binds = 0, tex_bytes = 0, reads = 0, read_bytes = 0, frames = 0, posts = 0;
        double us_draw = 0, us_frame = 0, us_read = 0, us_tex = 0, us_post = 0, us_other = 0;
    };
    static counters g_n;

    // EKA2L1_GPUMODEL_CALLS=1: every call counted by ordinal, and the state sets that set what was
    // already set (a bind of the bound texture, an enable of an enabled cap, the same colour, blend,
    // depth or texture-environment value): calls a client-side state cache would not make. State
    // starts unknown, so a first set is never redundant; a texture delete forgets the bindings.
    static int g_census = -1;
    static std::uint64_t g_ord[256], g_red[256];
    struct shadow {
        std::uint32_t cap[16][2];         // (cap, enabled) pairs seen
        std::uint32_t ncap = 0;
        std::uint32_t ccap[8][2];         // client states
        std::uint32_t nccap = 0;
        std::uint32_t bind = 0xFFFFFFFFu;
        std::uint32_t color[4] = { 0xFFFFFFFFu, 0, 0, 0 };
        std::uint32_t blend[2] = { 0xFFFFFFFFu, 0 };
        std::uint32_t alpha[2] = { 0xFFFFFFFFu, 0 };
        std::uint32_t depthf = 0xFFFFFFFFu, depthm = 0xFFFFFFFFu, mmode = 0xFFFFFFFFu;
        std::uint32_t env[8][2];          // (pname, value)
        std::uint32_t nenv = 0;
        std::uint32_t vptr[4] = { 0xFFFFFFFFu, 0, 0, 0 }, tptr[4] = { 0xFFFFFFFFu, 0, 0, 0 };
    };
    static shadow g_sh;

    // reverted sets: a state changed and changed back with no draw between -- both calls wasted. Each
    // tracked state (a key) keeps its value at the last draw; a set that returns it to that value after
    // it had moved counts 2.
    struct rkey { std::uint32_t key, cur, at_draw; bool moved; };
    static rkey g_rk[64];
    static std::uint32_t g_nrk = 0;
    static std::uint64_t g_rev[256];
    static void rk_set(const std::uint32_t ord, const std::uint32_t key, const std::uint32_t v) {
        for (std::uint32_t i = 0; i < g_nrk; i++) {
            rkey &k = g_rk[i];
            if (k.key != key) continue;
            if (k.cur == v) return;
            if (k.moved && v == k.at_draw) { g_rev[ord & 0xFFu] += 2; k.moved = false; }
            else k.moved = v != k.at_draw;
            k.cur = v;
            return;
        }
        if (g_nrk < 64) g_rk[g_nrk++] = rkey{ key, v, v, false };
    }
    static void rk_draw() {
        for (std::uint32_t i = 0; i < g_nrk; i++) { g_rk[i].at_draw = g_rk[i].cur; g_rk[i].moved = false; }
    }

    static bool census() {
        if (g_census < 0) {
            const char *c = std::getenv("EKA2L1_GPUMODEL_CALLS");
            g_census = (c && *c && *c != '0') ? 1 : 0;
        }
        return g_census == 1;
    }

    static bool set_cap(std::uint32_t (*t)[2], std::uint32_t &n, const std::uint32_t max, const std::uint32_t cap, const std::uint32_t on) {
        for (std::uint32_t i = 0; i < n; i++)
            if (t[i][0] == cap) {
                const bool same = t[i][1] == on;
                t[i][1] = on;
                return same;
            }
        if (n < max) {
            t[n][0] = cap;
            t[n][1] = on;
            n++;
        }
        return false;
    }

    static bool set_words(std::uint32_t *st, const std::uint32_t *a, const int n) {
        bool same = true;
        for (int i = 0; i < n; i++)
            if (st[i] != a[i])
                same = false;
        for (int i = 0; i < n; i++)
            st[i] = a[i];
        return same;
    }

    static void census_call(const std::uint32_t ordinal, const std::uint32_t *a) {
        const std::uint32_t k = ordinal & 0xFFu;
        g_ord[k]++;
        bool red = false;
        switch (ordinal) {     // reverted-set keys: the cap, or the ordinal's own state (and texenv's pname)
        case 0x1137: case 0x1133: rk_set(ordinal, 0x10000u | a[0], ordinal == 0x1137 ? 1u : 0u); break;
        case 0x111C: rk_set(ordinal, 0x20000u, a[1]); break;
        case 0x1126: rk_set(ordinal, 0x30000u, a[0] ^ (a[1] << 8) ^ (a[2] << 16) ^ (a[3] << 24) ^ a[3]); break;
        case 0x111D: rk_set(ordinal, 0x40000u, a[0] ^ (a[1] << 16)); break;
        case 0x112F: rk_set(ordinal, 0x50000u, a[0]); break;
        case 0x1130: rk_set(ordinal, 0x60000u, a[0]); break;
        case 0x117A: rk_set(ordinal, 0x70000u | (a[1] & 0xFFFFu), a[2]); break;
        case 0x1135: case 0x1136: rk_draw(); break;
        default: break;
        }
        switch (ordinal) {
        case 0x1137: red = set_cap(g_sh.cap, g_sh.ncap, 16, a[0], 1); break;          // glEnable
        case 0x1133: red = set_cap(g_sh.cap, g_sh.ncap, 16, a[0], 0); break;          // glDisable
        case 0x1138: red = set_cap(g_sh.ccap, g_sh.nccap, 8, a[0], 1); break;         // glEnableClientState
        case 0x1134: red = set_cap(g_sh.ccap, g_sh.nccap, 8, a[0], 0); break;         // glDisableClientState
        case 0x111C: red = g_sh.bind == a[1]; g_sh.bind = a[1]; break;                // glBindTexture
        case 0x112E: g_sh.bind = 0xFFFFFFFFu; break;                                  // glDeleteTextures
        case 0x1126: red = set_words(g_sh.color, a, 4); break;                        // glColor4x
        case 0x111D: red = set_words(g_sh.blend, a, 2); break;                        // glBlendFunc
        case 0x111B: red = set_words(g_sh.alpha, a, 2); break;                        // glAlphaFuncx
        case 0x112F: red = g_sh.depthf == a[0]; g_sh.depthf = a[0]; break;            // glDepthFunc
        case 0x1130: red = g_sh.depthm == a[0]; g_sh.depthm = a[0]; break;            // glDepthMask
        case 0x1159: red = g_sh.mmode == a[0]; g_sh.mmode = a[0]; break;              // glMatrixMode
        case 0x117A: red = set_cap(g_sh.env, g_sh.nenv, 8, a[1], a[2]); break;        // glTexEnvx(target, pname, param)
        case 0x1182: red = set_words(g_sh.vptr, a, 4); break;                         // glVertexPointer
        case 0x1177: red = set_words(g_sh.tptr, a, 4); break;                         // glTexCoordPointer
        default: break;
        }
        if (red)
            g_red[k]++;
    }

    static void census_report(const double f) {
        std::uint64_t tot = 0, totr = 0, totv = 0;
        char line[1024];
        int o = 0;
        for (int round = 0; round < 14; round++) {
            int best = -1;
            for (int k = 0; k < 256; k++)
                if (g_ord[k] && (best < 0 || g_ord[k] > g_ord[best]))
                    best = k;
            if (best < 0)
                break;
            if (o < 900)
                o += std::snprintf(line + o, sizeof(line) - o, " %04X %.1f/%.1f/%.1f", 0x1100 + best, g_ord[best] / f, g_red[best] / f,
                    g_rev[best] / f);
            totv += g_rev[best];
            g_rev[best] = 0;
            tot += g_ord[best];
            totr += g_red[best];
            g_ord[best] = 0;
            g_red[best] = 0;
        }
        for (int k = 0; k < 256; k++) {
            tot += g_ord[k];
            totr += g_red[k];
            totv += g_rev[k];
            g_ord[k] = g_red[k] = g_rev[k] = 0;
        }
        std::fprintf(stderr, "EKA2L1_GPUMODEL_CALLS: a frame %.0f calls, %.1f redundant, %.1f reverted; by ordinal (calls/redundant/reverted):%s\n",
            tot / f, totr / f, totv / f, line);
    }

    static void report() {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(5))
            return;
        last = now;
        const double f = g_n.frames ? static_cast<double>(g_n.frames) : 1.0;
        std::fprintf(stderr, "EKA2L1_GPUMODEL: %llu frames rendered, %llu posts; a frame: %.0f calls, %.1f draws, %.0f vertices, %.1f binds, "
                             "%.0f texture bytes, %.1f reads (%.0f bytes); us a frame: draw %.0f, tile render %.0f, readback %.0f, texture %.0f, post %.0f, other %.0f\n",
            (unsigned long long)g_n.frames, (unsigned long long)g_n.posts, g_n.calls / f, g_n.draws / f, g_n.verts / f, g_n.binds / f,
            g_n.tex_bytes / f, g_n.reads / f, g_n.read_bytes / f, g_n.us_draw / f, g_n.us_frame / f, g_n.us_read / f, g_n.us_tex / f,
            g_n.us_post / f, g_n.us_other / f);
        if (census())
            census_report(f);
        g_n = counters{};
    }

    static void init() {
        static bool done = false;
        if (done)
            return;
        done = true;
        const char *w = std::getenv("EKA2L1_GPUMODEL");
        if (!w || !*w)
            return;
        double v[9] = { 0 };
        int n = 0;
        const char *p = w;
        while (n < 9 && *p) {
            char *e = nullptr;
            v[n] = std::strtod(p, &e);
            n++;
            if (!e || *e != ',')
                break;
            p = e + 1;
        }
        g_params = params{ v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8] };
        g_on = true;
        std::fprintf(stderr, "EKA2L1_GPUMODEL: call %.1f us, draw %.1f us + %.0f ns a vertex, bind %.1f us, fill %.0f us a frame + "
                             "%.0f ns a triangle, readback %.1f ns a byte, texture %.1f ns a byte, post %.0f us\n",
            v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
    }

    bool enabled() {
        init();
        return g_on;
    }

    const params &get() {
        init();
        return g_params;
    }

    void pay_us(const double us, const std::uint64_t host_ns) {
        if (us <= 0)
            return;
        const std::uint64_t rate = cycle_tick_ips();
        if (!rate) {                 // wall-clock bench: stretched now under the CPU cap
            cpu_cap_pay_service(static_cast<std::uint64_t>(us * 1000.0), host_ns);
            return;
        }
        g_owed_us += us;
        const std::uint64_t whole = static_cast<std::uint64_t>(g_owed_us * static_cast<double>(rate));
        if (whole) {
            add_cpu_cycles(whole);
            g_owed_us -= static_cast<double>(whole) / static_cast<double>(rate);
        }
    }

    static std::uint32_t bytes_per_pixel(const std::uint32_t format, const std::uint32_t type) {
        // GL_UNSIGNED_SHORT_5_6_5 / 4_4_4_4 / 5_5_5_1: 2; GL_UNSIGNED_BYTE: by format
        if (type == 0x8363 || type == 0x8033 || type == 0x8034)
            return 2;
        switch (format) {
        case 0x1906: case 0x1909: return 1;       // ALPHA, LUMINANCE
        case 0x190A: return 2;                    // LUMINANCE_ALPHA
        case 0x1907: return 3;                    // RGB
        default: return 4;                        // RGBA
        }
    }

    double gl_call_us(const std::uint32_t ordinal, const std::uint32_t *a) {
        if (!enabled())
            return 0;
        const params &p = g_params;
        double us = p.call_us;
        g_n.calls++;
        if (census())
            census_call(ordinal, a);
        g_n.us_other += p.call_us;
        const auto frame = [&]() {           // the tile render a read of the frame waits for
            if (!g_dirty)
                return;
            const double t = p.fill_us + g_tris * p.tri_ns / 1000.0;
            us += t;
            g_n.us_frame += t;
            g_n.frames++;
            g_tris = 0;
            g_dirty = false;
        };
        switch (ordinal) {
        case 0x1135:                                  // glDrawArrays(mode, first, count)
        case 0x1136: {                                // glDrawElements(mode, count, type, indices)
            const std::uint32_t count = ordinal == 0x1135 ? a[2] : a[1];
            const double t = p.draw_us + count * p.vert_ns / 1000.0;
            us += t;
            g_n.us_draw += t;
            g_n.draws++;
            g_n.verts += count;
            g_tris += count / 3;
            g_dirty = true;
            break;
        }
        case 0x111C:                                  // glBindTexture
            us += p.bind_us;
            g_n.us_other += p.bind_us;
            g_n.binds++;
            break;
        case 0x117C:                                  // glTexImage2D(target, level, ifmt, w, h, border, format, type, ...)
        case 0x117F: {                                // glTexSubImage2D(target, level, x, y, w, h, format, type, ...)
            const double bytes = ordinal == 0x117C ? static_cast<double>(a[3]) * a[4] * bytes_per_pixel(a[6], a[7])
                                                   : static_cast<double>(a[4]) * a[5] * bytes_per_pixel(a[6], a[7]);
            const double t = bytes * p.texture_ns_per_byte / 1000.0;
            us += t;
            g_n.us_tex += t;
            g_n.tex_bytes += static_cast<std::uint64_t>(bytes);
            break;
        }
        case 0x116B: {                                // glReadPixels(x, y, w, h, format, type, pixels)
            // the first read after drawing waits for the tile render (once a frame: a
            // composite that reads back in strips pays it once), every read its copy
            frame();
            const double bytes = static_cast<double>(a[2]) * a[3] * bytes_per_pixel(a[4], a[5]);
            const double t = bytes * p.readback_ns_per_byte / 1000.0;
            us += t;
            g_n.us_read += t;
            g_n.reads++;
            g_n.read_bytes += static_cast<std::uint64_t>(bytes);
            break;
        }
        case 0x1139:                                  // glFinish
        case 0x1115:                                  // eglSwapBuffers
            frame();
            break;
        default:
            break;
        }
        report();
        return us;
    }

    double screen_post_us() {
        if (!enabled())
            return 0;
        g_n.posts++;
        g_n.us_post += g_params.post_us;
        return g_params.post_us;
    }
}
