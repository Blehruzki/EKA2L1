#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int32_t mid_ang2048(int32_t a, int32_t b)
{
    int32_t d = (int32_t)((uint32_t)(b - a) & 2047u);
    if (d >= 1024) d -= 2048;
    return a + d / 2;
}
int main(void) {
    struct { int32_t a, b, want; } t[] = {
        {2040, 10, 2049}, {10, 2040, 1}, {2131, 2120, 2126}, {533, 541, 537}, {-30, -26, -28},
        {0, 2047, 0}, {2047, 0, 2047}, {100, 300, 200}, {300, 100, 200}, {2100, 40, 2094}, {-33, 2040, -21} };
    int bad = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        int32_t g = mid_ang2048(t[i].a, t[i].b);
        /* same angle mod 2048 is acceptable */
        if (((g - t[i].want) & 2047) != 0) { printf("FAIL a=%d b=%d got %d want %d\n", t[i].a, t[i].b, g, t[i].want); bad++; }
    }
    /* the old 4096 rule on the same wrap, to show the case it got wrong */
    int32_t a = 2040, b = 10, d = b - a; if (d > 2048) d -= 4096; else if (d < -2048) d += 4096;
    printf("old rule on 2040->10: %d (half round from %d)\n", a + d / 2, a);
    printf("%s\n", bad ? "FAILED" : "all pass"); return bad;
}
