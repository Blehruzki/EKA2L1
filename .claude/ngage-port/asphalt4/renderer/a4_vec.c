/* a4_vec.c -- ARM rewrites of two hot fixed-point vector leaves in Asphalt 4's
 * Lib3D math library. Both are pure (operate only on their pointer arguments,
 * no globals), so they are position-independent whole-function drop-ins with a
 * clean AAPCS contract -- injected by a plain entry veneer, no trampoline.
 *
 *   +51D96  vec_add(int a[4], const int b[4])        : a[i] += b[i]
 *   +51DB8  vec_mulsub(int a[4], int s, const int b[4]): a[i] -= s*b[i]
 *
 * The shipped Thumb versions do 8 separate ldr/str per call (register-starved);
 * compiled for ARM, clang folds the four elements into ldmia/stmia bursts, so
 * the four scattered loads become one multi-word transfer -- fewer instructions
 * on the bench and a far friendlier memory pattern on the ARM11. Verified
 * bit-exact against the originals by renderer/a4_vec_verify.py.
 *
 * Build: clang --target=armv5te-none-eabi -marm -mtune=arm1136j-s -O2 \
 *              -ffreestanding -fno-builtin -fropi -c a4_vec.c -o a4_vec.o
 */
#include <stdint.h>

void a4_vec_add(int32_t *a, const int32_t *b)
{
    a[0] += b[0];
    a[1] += b[1];
    a[2] += b[2];
    a[3] += b[3];
}

void a4_vec_mulsub(int32_t *a, int32_t s, const int32_t *b)
{
    a[0] -= s * b[0];
    a[1] -= s * b[1];
    a[2] -= s * b[2];
    a[3] -= s * b[3];
}
