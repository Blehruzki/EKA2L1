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
        .word   36      @ [32] glDisable (A4_WINDOW: the fps bar is untextured quads)
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
