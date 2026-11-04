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
        .word   14      @ [0]  eglGetDisplay
        .word   17      @ [1]  eglInitialize
        .word   4       @ [2]  eglCreatePbufferSurface
        .word   3       @ [3]  eglCreateContext
        .word   18      @ [4]  eglMakeCurrent
        .word   132     @ [5]  glViewport
        .word   33      @ [6]  glClearColorx
        .word   31      @ [7]  glClear
        .word   58      @ [8]  glFinish
        .word   108     @ [9]  glReadPixels
        .word   67      @ [10] glGenTextures
        .word   29      @ [11] glBindTexture
        .word   125     @ [12] glTexImage2D
        .word   127     @ [13] glTexParameterx
        .word   56      @ [14] glEnable
        .word   90      @ [15] glMatrixMode
        .word   82      @ [16] glLoadIdentity
        .word   99      @ [17] glOrthox
        .word   57      @ [18] glEnableClientState
        .word   131     @ [19] glVertexPointer
        .word   120     @ [20] glTexCoordPointer
        .word   39      @ [21] glColor4x
        .word   54      @ [22] glDrawArrays
        .word   116     @ [23] glShadeModel
        .word   123     @ [24] glTexEnvx
        .word   68      @ [25] glGetError
        .word   1       @ [26] eglChooseConfig  (batched renderer: depth config)
        .word   55      @ [27] glDrawElements   (batched indexed draw)
        .word   48      @ [28] glDepthFunc
        .word   35      @ [29] glClearDepthx
        .word   6       @ [30] eglCreateWindowSurface (A4_WINDOW: the GPU presents)
        .word   22      @ [31] eglSwapBuffers
        .word   52      @ [32] glDisable (was 36 = glClearStencil until round 10; only the
                        @      unshipped A4_WINDOW fps bar used it; flat prims need it now)
        .word   28      @ [33] glAlphaFuncx (A4_TEL colorkey: GPU alpha test for sprites)
        .word   128     @ [34] glTexSubImage2D (A4_TEL colorkey: strip-upload 5551 any size)
        .word   30      @ [35] glBlendFunc   (additive prims; between 29 glBindTexture and
                        @      31 glClear, both proven on the N95 -- the only GL ES 1.1 name there)
        .word   59      @ [36] glFlush       (kick the tile renderer at the frame end)
        .word   69      @ [37] glGetIntegerv (the implementation's read format, OES_read_format)
        .word   117     @ [38] glStencilFunc  (shadows darken a pixel once a frame, as the
        .word   119     @ [39] glStencilOp     game's per-frame stamp map does)
        .word   36      @ [40] glClearStencil
        .word   40      @ [41] glColorMask    (round 11: blended classes leave dst alpha alone)
        .word   84      @ [42] glLoadMatrixx  (A4_MESH: the frame's projection, a key's texcoord scale)
        .word   49      @ [43] glDepthMask    (A4_MESH: the ordering table's classes test, not write)
        .global a4_gl_iat_end
a4_gl_iat_end:

@ ws32 import address table (A4_WINDOW): the window the GPU presents into is our
@ own -- a session, a window group that declines focus, a full-screen window --
@ so nothing of the runtime's has to be found. Ordinals from the window server's
@ eabi def (WS322U.DEF); the N-Gage DLLs import the same numbers (runtime.dll
@ 70/76, ngidisplay 81/99/211/277), which is the check that they are 9.2's.
        .global a4_ws_iat
        .align  2
a4_ws_iat:
        .word   76      @ [0]  RWsSession::RWsSession()
        .word   70      @ [1]  RWsSession::Connect()
        .word   69      @ [2]  RWsSession::Flush()
        .word   158     @ [3]  RWindowGroup::RWindowGroup(RWsSession&)
        .word   157     @ [4]  RWindowGroup::Construct(TUint32, TBool)
        .word   148     @ [5]  RWindowGroup::EnableReceiptOfFocus(TBool)
        .word   211     @ [6]  RWindowTreeNode::SetOrdinalPosition(TInt)
        .word   278     @ [7]  RWindow::RWindow(RWsSession&)
        .word   275     @ [8]  RWindow::Construct(const RWindowTreeNode&, TUint32)
        .word   277     @ [9]  RWindow::SetExtent(const TPoint&, const TSize&)
        .word   107     @ [10] RWindowBase::Activate()
        .word   81      @ [11] RWindowBase::SetVisible(TBool)
        .word   99      @ [12] RWindowBase::SetRequiredDisplayMode(TDisplayMode)
        .word   268     @ [13] RWindow::SetNonTransparent()
        .word   212     @ [14] RWindowTreeNode::SetOrdinalPosition(TInt, TInt): position AND priority
        .word   197     @ [15] RBackedUpWindow::Construct(parent, TDisplayMode, TUint32)
        .global a4_ws_iat_end
a4_ws_iat_end:
@ cone import address table (A4_WINDOW): CCoeEnv::Static() and RootWin() give the
@ app's own window-server session and root window group, so the EGL window is
@ created in the session libGLES_CM uses internally (CCoeEnv::Static()->WsSession),
@ which a second session is not -- the device's WSERV 6 (invalid window handle).
        .global a4_cone_iat
        .align  2
a4_cone_iat:
        .word   182     @ [0] CCoeEnv::Static()   (RootWin 717 removed: absent on S60 3.1, breaks the device loader)
        .global a4_cone_iat_end
a4_cone_iat_end:

@ efsrv import address table (A4_TEL): the build has no efsrv of its own, so the
@ injector adds one to write the telemetry files the owner retrieves. Ordinals
@ from the fileserver eabi def; all <= the N95 efsrv.dll's 351 exports, so the
@ same functions there (ordinals are append-only). RFs::Close (390) is past that
@ count and is NOT used -- the files stay open and are flushed after each write.
        .global a4_efs_iat
        .align  2
a4_efs_iat:
        .word   68      @ [0] RFs::Connect(TInt)
        .word   108     @ [1] RFile::Replace(RFs&, const TDesC16&, TUint)
        .word   101     @ [2] RFile::Write(TInt aPos, const TDesC8&)  (positioned, sync)
        .word   96      @ [3] RFile::Flush()
        .global a4_efs_iat_end
a4_efs_iat_end:
        .global a4_efs_iat_ptr
        .align  2
a4_efs_iat_ptr:
        ldr     r0, 8f
7:      add     r0, pc, r0          @ pc here = 7b + 8 (ARM)
        bx      lr
8:      .word   a4_efs_iat - (7b + 8)

        .global a4_cone_iat_ptr
        .align  2
a4_cone_iat_ptr:
        ldr     r0, 6f
5:      add     r0, pc, r0          @ pc here = 5b + 8 (ARM)
        bx      lr
6:      .word   a4_cone_iat - (5b + 8)

        .global a4_ws_iat_ptr
        .align  2
a4_ws_iat_ptr:
        ldr     r0, 4f
3:      add     r0, pc, r0          @ pc here = 3b + 8 (ARM)
        bx      lr
4:      .word   a4_ws_iat - (3b + 8)

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

@ a4_ntick() -> User::NTickCount(), the nanokernel tick (its period on the N95 is not
@ known -- a4_tel.c records it beside User::TickCount so the pair calibrates it), for
@ timing the GL phases on the phone. Called as its fast exec directly, so no import is added: euser's own export
@ (RM-320 ordinal 2103, read off the ROM -- the Thumb export at 0x803a855a BLXes the
@ ARM stub at 0x8039fb18, `swi 0x800010; bx lr`) is exactly this. Fast exec 0x0F
@ (ordinal 584, FastCounter) is NOT used: it answers a constant on the N95
@ (DEVICES.md, round 152). ARM state: EKA2 reads a 24-bit SWI number only from an
@ ARM SWI.
        .global a4_ntick
        .align  2
a4_ntick:
        swi     0x800010
        bx      lr

@ A4_MESH: where the game's code is, from here -- each word is patched by the injector
@ to (target link address) - (the word's own link address), so &word + word is the
@ target at run time wherever the image loads (a4_gl.c adds them).
        .global a4_otins, a4_tfvt, a4_bbret, a4_tfzvt
        .align  2
a4_otins:
        .word   0xDEADBEEF          @ patched: (code+0x349B0 | 1) - (this word): the ordering-table insert
a4_tfvt:
        .word   0xDEADBEEF          @ patched: (code+0xB08EC) - (this word): TFace's vtable
a4_bbret:
        .word   0xDEADBEEF          @ patched: (code+0x31364 | 1) - (this word): TBillboard's blit call returns here
a4_tfzvt:
        .word   0xDEADBEEF          @ patched: (code+0xB08CC) - (this word): TFakeZFace's vtable (its key is 1)

@ a4_gcall1(fn, a0): call a game function with one argument and keep r4-r11 across it. The
@ span trampolines (a4_tramp.s) run inside the game's render and use r8-r11 without saving
@ them -- the game's Thumb code never holds those across a span, but our ARM code that calls
@ the render does (round 31: a4_step_render's fp came back as 0xAAACC and its epilogue took
@ the stack with it). 10 words pushed: the stack stays 8-aligned.
        .global a4_gcall1
        .align  2
a4_gcall1:
        push    {r3, r4, r5, r6, r7, r8, r9, r10, r11, lr}
        mov     r12, r0
        mov     r0, r1
        blx     r12
        pop     {r3, r4, r5, r6, r7, r8, r9, r10, r11, pc}

@ A4_STEPTIME (round 30): the race update's callees, patched the same way in every GL build
        .global a4_stepcnt, a4_stepmain, a4_stepcam, a4_steprend
        .align  2
a4_stepcnt:
        .word   0xDEADBEEF          @ patched: (code+0x15B1C | 1) - (this word): the step count
a4_stepmain:
        .word   0xDEADBEEF          @ patched: (code+0x15A52 | 1) - (this word): called once a step
a4_stepcam:
        .word   0xDEADBEEF          @ patched: (code+0x12D5A | 1) - (this word): called before the render
a4_steprend:
        .word   0xDEADBEEF          @ patched: (code+0x12992 | 1) - (this word): the render

@ A4_HALFMOTION (round 31): the step's car calls, patched the same way in every GL build
        .global a4_hc_1de78, a4_hc_1f1f8, a4_hc_1ea64, a4_hc_1fd5c
a4_hc_1de78:
        .word   0xDEADBEEF          @ patched: (code+0x1DE78 | 1) - (this word): a car's physics
a4_hc_1f1f8:
        .word   0xDEADBEEF          @ patched: (code+0x1F1F8 | 1) - (this word): its other physics
a4_hc_1ea64:
        .word   0xDEADBEEF          @ patched: (code+0x1EA64 | 1) - (this word): a car's movement
a4_hc_1fd5c:
        .word   0xDEADBEEF          @ patched: (code+0x1FD5C | 1) - (this word): its other movement
        .global a4_hc_1fd0e
a4_hc_1fd0e:
        .word   0xDEADBEEF          @ patched: (code+0x1FD0E | 1) - (this word): a car's physics in the +691E mode
        .global a4_hc_fb12
a4_hc_fb12:
        .word   0xDEADBEEF          @ patched: (code+0xFB12 | 1) - (this word): the traffic's step (round 41)
        .global a4_hc_25e8
a4_hc_25e8:
        .word   0xDEADBEEF          @ patched: (code+0x25E8 | 1) - (this word): the key handler
        .global a4_hc_12dec
a4_hc_12dec:
        .word   0xDEADBEEF          @ patched: (code+0x12DEC | 1) - (this word): the camera's update
