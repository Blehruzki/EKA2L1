@ a4_gl.s -- the libGLES_CM import address table for the MBX pbuffer spike.
@
@ These .word slots sit in .text (not .data, which the injector forbids). Each
@ holds a libGLES_CM ordinal; the injector appends an import entry whose import
@ blocks point at these code offsets, and the Symbian loader overwrites each
@ word in place with the resolved function address -- exactly as the game's own
@ IAT (also in its code section) is fixed up. a4_gl.c reads them back as
@ function pointers. The order here is the index order a4_gl.c expects, and the
@ injector reads the same order from GL_IMPORT_ORDINALS; the two must agree.
        .syntax unified
        .arm
        .text
        .global a4_gl_iat
        .align  2
a4_gl_iat:
        .word   14      @ [0] eglGetDisplay
        .word   17      @ [1] eglInitialize
        .word   4       @ [2] eglCreatePbufferSurface
        .word   3       @ [3] eglCreateContext
        .word   18      @ [4] eglMakeCurrent
        .word   132     @ [5] glViewport
        .word   33      @ [6] glClearColorx
        .word   31      @ [7] glClear
        .word   58      @ [8] glFinish
        .word   108     @ [9] glReadPixels
        .global a4_gl_iat_end
a4_gl_iat_end:

@ a4_gl_iat_ptr() -> &a4_gl_iat, computed pc-relatively so the blob stays fully
@ position independent. A C `extern a4_gl_iat[]` reference compiles to an
@ absolute literal-pool address that nothing relocates (the injector appends the
@ blob as-is), which faults at runtime; this getter is the fix.
        .global a4_gl_iat_ptr
        .align  2
a4_gl_iat_ptr:
        ldr     r0, 1f
0:      add     r0, pc, r0          @ pc here = 0b + 8 (ARM)
        bx      lr
1:      .word   a4_gl_iat - (0b + 8)
