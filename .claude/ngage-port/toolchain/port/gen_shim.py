#!/usr/bin/env python3
"""Generate the forwarding table gate 4 links against.

    gen_shim.py <image> <output.cpp>

For every import the old binary makes, shimtable finds the Symbian 9.x ordinal
of the same function.  This writes that out as data: a list of DLLs to load and
one word per import holding which DLL and which ordinal, or zero where nothing
matched.

The forwards are resolved at run time with RLibrary::Lookup rather than through
our own import section, for two reasons.  An import section is checked when the
image loads, so one ordinal that does not exist on the device would stop the
whole program from starting, with no way to say which; a lookup that returns
null can be reported.  And the ordinals come from the Symbian source, which is
a later 9.x than any one phone, so some of them will be wrong.
"""
import os, re, sys

import e32imports, epocdb, gnuv2, shimtable, symdef

ROOT = '/home/user/symbiansource'
KERNEL = ROOT + '/oss.fcl.sf.os.kernelhwsrv'
COMPSUPP = KERNEL + '/kernel/eka/compsupp/eabi'
EPOC6 = '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc6.def'
EPOC9 = '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc9.def'

# Each table entry is  kind << 24 | dll index << 16 | ordinal.
KIND_NONE, KIND_CALL, KIND_REM, KIND_LOCAL, KIND_ARG3 = 0, 1, 2, 3, 4
KIND_SRET8, KIND_ARGSHIFT = 5, 6
# Forward and post-process r0: negate (GCC's __lt*2 answers negative for
# less, EABI's __aeabi_*cmplt answers 1), decrement (__ge*2 answers >= 0 for
# greater-or-equal; cmpge's 1 -> 0, 0 -> -1), invert 1<->0 (__le*2 / __eq*2
# answer 0 for true), and sign-extend r1 into r2:r3 for a TInt that became a
# TInt64 (TDes8::Num).
KIND_NEG, KIND_DEC, KIND_NOT, KIND_SEXT1 = 7, 8, 9, 10
# A TUint that became a TUint64 with a TRadix after it: r1 goes to r2:r3
# zero-extended and the radix from r2 to the stack (One, round 125).
KIND_ZEXT1_RADIX = 11
# A double by value after `this`: GCC98r2 passed it in r1:r2, EABI aligns it
# to r2:r3, and the word after it (r3) goes to the stack. The words are in
# the FPA order where the title has it (GAME_FPA_DOUBLES).
KIND_DBL1 = 12

# Where the two calling conventions actually disagree. GCC98r2 returned an
# eight-byte structure in r0 and r1; EABI returns anything over four bytes
# through a hidden pointer the caller passes in r0, pushing `this` to r1. The
# demangled names carry no return type, so the ones that do this are listed.
# Only no-argument members so far -- anything with arguments would have to
# shift them along as well.
RETURNS_STRUCT = {
    'TParseBase::DriveAndPath() const',
    # The rest of TParseBase's TPtrC getters, the same eight-byte TPtrC
    # (FullName returns a reference and is not one). One, E473: Name()
    # forwarded as a plain call read its hidden-pointer argument off the
    # game's `this`.
    'TParseBase::Name() const',
    'TParseBase::Ext() const',
    'TParseBase::NameAndExt() const',
    'TParseBase::Path() const',
    'TParseBase::Drive() const',
    # TProcessId wraps a TUint64, so it is eight bytes: two registers under
    # GCC98r2, a hidden pointer under EABI.
    'RProcess::Id(void) const',
    # TPtrC16 / TPtrC8 by value from the descriptor slicers (One, E516:
    # Left read its length register as `this`). The stub carries up to two
    # arguments after `this` across.
    'TDesC16::Left(int) const', 'TDesC16::Right(int) const',
    'TDesC16::Mid(int) const', 'TDesC16::Mid(int, int) const',
    'TDesC8::Left(int) const', 'TDesC8::Right(int) const',
    'TDesC8::Mid(int) const', 'TDesC8::Mid(int, int) const',
}

# A double by value after `this` lands two registers apart in the two ABIs
# (r1:r2, and r2:r3 under EABI's alignment), and in One its words are crossed
# as well. Forwarded plainly, TRealX(double) read the low word and r3 as the
# value, so the double-to-integer at 0xaae24 answered rubbish. These match by
# ordinal, so the kind is laid over the match the way RETURNS_STRUCT's is.
PASSES_DOUBLE = {
    'TRealX::TRealX(double)',
    'TDes16::AppendNum(double, TRealFormat const &)',
    'TDes8::AppendNum(double, TRealFormat const &)',
    'TDes16::Num(double, TRealFormat const &)',
    'TDes8::Num(double, TRealFormat const &)',
}

# GCC98r2 put its compiler helpers in euser; EABI puts them in the runtime
# libraries the SDK links against, under their AEABI names. Same routines,
# same registers -- a double arrives in r0:r1 and r2:r3 either way -- so most
# are a straight forward.
HELPERS = {
    '__adddf3':      ('dfpaeabi', '__aeabi_dadd',     KIND_CALL),
    '__addsf3':      ('dfpaeabi', '__aeabi_fadd',     KIND_CALL),
    '__divsf3':      ('dfpaeabi', '__aeabi_fdiv',     KIND_CALL),
    '__muldf3':      ('dfpaeabi', '__aeabi_dmul',     KIND_CALL),
    '__mulsf3':      ('dfpaeabi', '__aeabi_fmul',     KIND_CALL),
    '__extendsfdf2': ('dfpaeabi', '__aeabi_f2d',      KIND_CALL),
    '__truncdfsf2':  ('dfpaeabi', '__aeabi_d2f',      KIND_CALL),
    '__fixsfsi':     ('dfpaeabi', '__aeabi_f2iz',     KIND_CALL),
    '__floatsidf':   ('dfpaeabi', '__aeabi_i2d',      KIND_CALL),
    '__floatsisf':   ('dfpaeabi', '__aeabi_i2f',      KIND_CALL),
    # One is the first title to import the double-to-int truncation; the
    # FPA word order is handled by the same hook as __fixsfsi's kind.
    '__fixdfsi':     ('dfpaeabi', '__aeabi_d2iz',     KIND_CALL),
    # __aeabi_idiv and __aeabi_uidiv sit at the end of the def and are absent
    # from shipped drtaeabi builds. divmod returns the quotient in r0, which is
    # exactly what these want, so use it and ignore the remainder in r1.
    '__divsi3':      ('drtaeabi', '__aeabi_idivmod',  KIND_CALL),
    '__udivsi3':     ('drtaeabi', '__aeabi_uidivmod', KIND_CALL),
    '__divdi3':      ('drtaeabi', '__aeabi_ldivmod',  KIND_CALL),
    # idivmod returns the quotient in r0 and the remainder in r1; __modsi3 has
    # to return the remainder, so these get a thunk that moves it across.
    '__modsi3':      ('drtaeabi', '__aeabi_idivmod',  KIND_REM),
    '__umodsi3':     ('drtaeabi', '__aeabi_uidivmod', KIND_REM),
    # operator new and delete, which EABI keeps in their own library.
    '__builtin_new':        ('scppnwdl', '_Znwj', KIND_CALL),
    '__builtin_delete':     ('scppnwdl', '_ZdlPv', KIND_CALL),
    '__builtin_vec_delete': ('scppnwdl', '_ZdaPv', KIND_CALL),
    '__builtin_vec_new':    ('scppnwdl', '_Znaj',  KIND_CALL),
    # Ashen's share of the GCC98r2 float library, round 109.
    '__subdf3':      ('dfpaeabi', '__aeabi_dsub',     KIND_CALL),
    '__subsf3':      ('dfpaeabi', '__aeabi_fsub',     KIND_CALL),
    '__divdf3':      ('dfpaeabi', '__aeabi_ddiv',     KIND_CALL),
    '__ltsf2':       ('dfpaeabi', '__aeabi_fcmplt',   KIND_NEG),
    '__ltdf2':       ('dfpaeabi', '__aeabi_dcmplt',   KIND_NEG),
    '__gtsf2':       ('dfpaeabi', '__aeabi_fcmpgt',   KIND_CALL),
    '__gtdf2':       ('dfpaeabi', '__aeabi_dcmpgt',   KIND_CALL),
    '__gesf2':       ('dfpaeabi', '__aeabi_fcmpge',   KIND_DEC),
    '__gedf2':       ('dfpaeabi', '__aeabi_dcmpge',   KIND_DEC),
    '__lesf2':       ('dfpaeabi', '__aeabi_fcmple',   KIND_NOT),
    '__ledf2':       ('dfpaeabi', '__aeabi_dcmple',   KIND_NOT),
    '__eqsf2':       ('dfpaeabi', '__aeabi_fcmpeq',   KIND_NOT),
    '__eqdf2':       ('dfpaeabi', '__aeabi_dcmpeq',   KIND_NOT),
    '__nesf2':       ('dfpaeabi', '__aeabi_fcmpeq',   KIND_NOT),
    '__nedf2':       ('dfpaeabi', '__aeabi_dcmpeq',   KIND_NOT),
}

# Classes the game derives from, whose constructors and destructors must not be
# forwarded.
#
# Forwarding a leaf function is sound: allocation, descriptors, arithmetic and
# file I/O have the same layout either side. Forwarding a base-class
# constructor is not. The game allocates its application object at 556 bytes --
# the size the 7.0s compiler computed -- and then calls the base constructor;
# sending that to the 9.x one runs 9.x code writing 9.x field offsets into an
# object that was never laid out that way. Nothing makes iCoeEnv and
# iResourceFileOffset sit where the old code expects them.
#
# Until each is reimplemented against the old layout, an empty body on zeroed
# memory is the closer approximation: it leaves the old fields at zero rather
# than filling them with values meant for a different object.
FRAMEWORK_BASES = ('CCoeControl', 'CCoeAppUi', 'CEikApplication', 'CEikDocument',
                   'CEikAppUi', 'CEikDialog', 'CEikBorderedControl',
                   'CAknApplication', 'CAknDocument', 'CAknAppUi')

# No EABI routine matches these, so gate 4 generates them itself.
LOCAL_NEGSF2, LOCAL_PURE_VIRTUAL, LOCAL_NOOP, LOCAL_MEM_COMPARE = 0, 1, 2, 3
LOCAL_TRAP_ENTER, LOCAL_TINT64_SET = 4, 5
LOCAL_TRUE = 6
LOCAL_SELF = 7
LOCAL_MEM_MOVE = 8
LOCAL_TRAP_UNTRAP = 9
LOCAL_RAMSIZES = 10
LOCAL_SYSAGT_SETSTATUS, LOCAL_SYSAGT_NOTIFY, LOCAL_SYSAGT_CANCEL = 11, 12, 13
# One (round 125). __negdf2 flips the sign word in the EABI position; the
# FPA hook around it (GAME_FPA_DOUBLES) turns that into the game's order.
# The TInt64 operators are the EKA1 class's -- {iLow, iHigh}, the three
# that return a TInt64 through a hidden pointer in r0 with `this` in r1
# (operator-, operator*, operator/ as the game calls them at 0x11a2c,
# 0x119d8, 0x119c4) -- in C in gate6.cpp. GetTReal answers in the FPA order.
LOCAL_NEGDF2, LOCAL_TINT64_LOW, LOCAL_TINT64_REAL = 14, 15, 16
LOCAL_TINT64_ADD, LOCAL_TINT64_SUB, LOCAL_TINT64_MUL, LOCAL_TINT64_DIV = 17, 18, 19, 20
LOCAL_TINT64_GE, LOCAL_TINT64_LT = 21, 22
LOCAL_SET_WORD_2C, LOCAL_GET_WORD_2C = 23, 24
LOCAL_VOLUMEINFO_CTOR = 25
LOCAL = {'__negsf2': LOCAL_NEGSF2, '__pure_virtual': LOCAL_PURE_VIRTUAL,
         'memmove': LOCAL_MEM_MOVE, '__negdf2': LOCAL_NEGDF2}

# Functions 9.x kept but moved, renamed or gave another argument. Each was
# checked against the 9.x def rather than assumed; the rest of what does not
# match is genuinely gone (CServer, CSession, TTrap, TInt64) and needs writing.
MANUAL = {
    # Colin McRae's engine (E891). 9.x made these RWsSession and RWindow
    # getters const, which is all that kept them from pairing.
    'RWsSession::GetEvent(TWsEvent &)': ('ws32', 'RWsSession::GetEvent(TWsEvent&) const', KIND_CALL),
    'RWsSession::GetFocusWindowGroup()': ('ws32', 'RWsSession::GetFocusWindowGroup() const', KIND_CALL),
    'RWsSession::GetPriorityKey(TWsPriorityKeyEvent &)':
        ('ws32', 'RWsSession::GetPriorityKey(TWsPriorityKeyEvent&) const', KIND_CALL),
    'RWsSession::FetchMessage(TUid &, TPtr8 &, const TWsEvent &)':
        ('ws32', 'RWsSession::FetchMessage(TUid&, TPtr8&, TWsEvent const&) const', KIND_CALL),
    'RWindow::GetInvalidRegion(RRegion &)': ('ws32', 'RWindow::GetInvalidRegion(RRegion&) const', KIND_CALL),
    # A raw event into the window server needs SwEvent on 9.x, which no port
    # application has, and 9.x's TRawEvent carries iTicks after iType, so
    # the by-value event would arrive shifted anyway. Answered with nothing.
    'SimulateRawEvent__10RWsSessionG9TRawEvent': ('local', LOCAL_NOOP, KIND_LOCAL),
    # estlib's memset is euser's on 9.x; RThread::Rename became the static
    # User::RenameThread, of the calling thread (the engine renames its own).
    'memset': ('euser', 'memset', KIND_CALL),
    'RThread::Rename(TDesC16 const &) const': ('euser', 'User::RenameThread(TDesC16 const&)', KIND_ARGSHIFT),
    # The exception handler moved from RThread to User -- and with it went the
    # object. The game calls it on an RThread, so `this` is in r0 and the two
    # real arguments are in r1 and r2; User::SetExceptionHandler wants them in
    # r0 and r1. Forwarded as it stood, it installed the RThread's address as
    # the handler and the handler's address as the mask, and the first
    # exception after that jumped into nothing.
    'RThread::SetExceptionHandler(void (*)(TExcType), unsigned long)':
        ('euser', 'User::SetExceptionHandler(void (*)(TExcType), unsigned long)',
         KIND_ARGSHIFT),
    # RFsBase is the base of RFile, a *sub*session, and every title calls this
    # on an RFile (none imports RDir, RFormat or RRawDisk). It was mapped to
    # RHandleBase::Close, which closes word 0 -- the parent session's handle,
    # which 9.x marks KHandleNoClose in a subsession (us_mes.cpp,
    # DoCreateSubSession), so the kernel refused it and the file was never
    # closed: every file a title opened stayed open on the file server, and
    # on the bench One's fighter save lost its last unflushed bytes and was
    # deleted as corrupt at the next launch (round 127). RFile::Close is
    # CloseSubSession(EFsFileSubClose), efsrv 300 (RM-409 ROM, disassembled).
    'RFsBase::Close()': ('efsrv', 'RFile::Close()', KIND_CALL),
    # 9.x ReAllocL takes a mode as a third argument. Zero is the old behaviour.
    'User1::ReAlloc1L(void *, int)': ('euser', 'User::ReAllocL(void*, int, int)', KIND_ARG3),
    # And the non-leaving one beside it, which Asphalt 2 does not import and
    # Asphalt 1 dies on two frames in (E285).
    'User1::ReAlloc1(void *, int)': ('euser', 'User::ReAlloc(void*, int, int)', KIND_ARG3),
    # CBase's constructor and destructor are empty and 9.x stopped exporting them.
    'CBase1::CBase1(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CBase1::~CBase1(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    # Only the 16-bit Mem::Compare survives as an export, so do the 8-bit one here.
    'Mem::Compare(unsigned char const *, int, unsigned char const *, int)':
        ('local', LOCAL_MEM_COMPARE, KIND_LOCAL),
    # EKA1's trap harness has no 9.x counterpart -- a 9.x leave is a C++
    # throw, and a throw cannot unwind through the game's frames or ours:
    # rounds 106 to 108 died in std::terminate when SetAudioPropertiesL left
    # inside the game's TRAP. So the port runs EKA1's harness itself:
    # Trap is a setjmp into the game's TTrap and UnTrap pops it
    # (gate6_trap_enter / gate6_trap_untrap), and the port's own
    # TTrapHandler::Leave longjmps back before 9.x throws. Builds up to 024
    # faked both (Trap answered 0, UnTrap did nothing).
    'TTrap::Trap(int &)': ('local', LOCAL_TRAP_ENTER, KIND_LOCAL),
    'TTrap::UnTrap(void)': ('local', LOCAL_TRAP_UNTRAP, KIND_LOCAL),
    # An N-Gage bus device mixin with nothing behind it on a phone. A GCC98r2
    # constructor hands the object back, so this one only returns.
    'MBusDev::MBusDev(void)': ('local', LOCAL_SELF, KIND_LOCAL),
    # EKA1's TInt64 was a class of a low word and a high one; 9.x made it a
    # plain long long and stopped exporting anything. Setting one from a TInt
    # is a sign extension, and a GCC98r2 constructor returns the object.
    'TInt64::TInt64(int)': ('local', LOCAL_TINT64_SET, KIND_LOCAL),
    'TInt64::operator=(int)': ('local', LOCAL_TINT64_SET, KIND_LOCAL),

    # EKA1's client-server framework. 9.x replaced CServer, CSession and
    # RMessage wholesale with CServer2, CSession2 and RMessage2 -- different
    # classes, different virtuals, a different message object -- so not one of
    # them forwards, and every one of them was a reporting stub that panics.
    # The game's SoundServer thread builds a CServer, and through E113 that
    # panic killed the thread; the emulator let the process carry on, but a
    # phone would not.
    #
    # These are stand-ins, not an implementation. Both ends of this server live
    # inside our own process, so a real in-process bridge is possible and is
    # the right answer for audio -- CreateSession would find the server object
    # by name and SendReceive would call its session straight, with no kernel
    # IPC at all. This is the smaller thing that comes first: the sound thread
    # builds its object, sits in its own active scheduler and harms nothing.
    #
    # LOCAL_SELF for the constructors because a GCC98r2 constructor hands the
    # object back in r0. They leave the CActive base uninitialised, which would
    # matter if anything added the server to a scheduler -- but StartL is what
    # does that on real Symbian, and StartL here does nothing.
    # Ashen, round 109. Each of these has a 9.x function with the same job
    # under another name or shape.
    'RFs::ReadFileSection(const TDesC16 &, int, TDes8 &, int) const':
        ('efsrv', 'RFs::ReadFileSection_RESERVED(TDesC16 const&, int, TDes8&, int) const', KIND_CALL),
    # The G in the old mangling (a class passed by value) is beyond gnuv2, so
    # the key is the raw name. One word either way.
    'Flush__7CCoeEnvG27TTimeIntervalMicroSeconds32':
        ('cone', 'CCoeEnv::Flush(TTimeIntervalMicroSeconds32)', KIND_CALL),
    'TDes16::FormatList(TDesC16 const &, signed char **)':
        ('euser', 'TDes16::FormatList(TDesC16 const&, std::__va_list)', KIND_CALL),
    # TDes8::Num(TInt) is inline over Num(TInt64) now: the value goes to r2:r3.
    'TDes8::Num(int)': ('euser', 'TDes8::Num(long long)', KIND_SEXT1),
    # The current thread's heap, which is all the game ever asks about.
    'RThread::Heap(void)': ('euser', 'User::Allocator()', KIND_CALL),
    'RHeap1::Size(void) const': ('euser', 'RAllocator::Size() const', KIND_CALL),
    'RThread::GetRamSizes(int &, int &)': ('local', LOCAL_RAMSIZES, KIND_LOCAL),
    'CnvUtfConverter::ConvertFromUnicodeToUtf8(TDes8 &, const TDesC16 &)':
        ('charconv', 'CnvUtfConverter::ConvertFromUnicodeToUtf8(TDes8&, TDesC16 const&)', KIND_CALL),
    'CServer::CServer(int, CServer::TServerType)': ('local', LOCAL_SELF, KIND_LOCAL),
    'CServer::StartL(TDesC16 const &)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CServer::RunL(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CServer::DoCancel(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CServer::~CServer(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CSession::CSession(RThread)': ('local', LOCAL_SELF, KIND_LOCAL),
    'CSession::~CSession(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CSession::WriteL(void const *, TDesC8 const &, int) const':
        ('local', LOCAL_NOOP, KIND_LOCAL),
    # Never panic on the game's behalf: a session panic kills the client.
    'CSession::Panic(TDesC16 const &, int) const': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CSharableSession::CreateL(CServer const &)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'RMessage::Complete(int) const': ('local', LOCAL_NOOP, KIND_LOCAL),
    # KErrNone. RSessionBase::CreateSession is deliberately left forwarding to
    # the real 9.x export, which will answer KErrNotFound because no kernel
    # server of that name exists -- that is the honest answer and the game may
    # well have a path for it. If it turns out not to, this is where pretending
    # would go, and the two have to agree.
    'RSessionBase::SendReceive(int, void *) const': ('local', LOCAL_NOOP, KIND_LOCAL),

    # One, round 125. The TInt-to-TInt64 family, as TDes8::Num above: the
    # value sign-extends into r2:r3; with a TRadix behind it the TUint
    # zero-extends there and the radix moves from r2 to the stack.
    'TDes16::Num(int)': ('euser', 'TDes16::Num(long long)', KIND_SEXT1),
    'TDes16::AppendNum(int)': ('euser', 'TDes16::AppendNum(long long)', KIND_SEXT1),
    'TDes8::AppendNum(int)': ('euser', 'TDes8::AppendNum(long long)', KIND_SEXT1),
    'TDes16::AppendNum(unsigned int, TRadix)':
        ('euser', 'TDes16::AppendNum(unsigned long long, TRadix)', KIND_ZEXT1_RADIX),
    'TDes8::AppendNum(unsigned int, TRadix)':
        ('euser', 'TDes8::AppendNum(unsigned long long, TRadix)', KIND_ZEXT1_RADIX),
    'TDes16::NumUC(unsigned int, TRadix)':
        ('euser', 'TDes16::NumUC(unsigned long long, TRadix)', KIND_ZEXT1_RADIX),
    # The EKA1 TInt64 {iLow, iHigh} and a long long are the same eight bytes,
    # so a reference to one is a reference to the other. FRand answers a
    # double in r0:r1, which the FPA hook (IMPORT_MATH_FRAND) re-orders.
    'Math::Rand(TInt64 &)': ('euser', 'Math::Rand(long long&)', KIND_CALL),
    'Math::FRand(TInt64 &)': ('euser', 'Math::FRand(long long&)', KIND_CALL),
    # The rest of the class is gone from 9.x: written locally, see LOCAL_TINT64_*.
    'TInt64::GetTInt(void) const': ('local', LOCAL_TINT64_LOW, KIND_LOCAL),
    'TInt64::GetTReal(void) const': ('local', LOCAL_TINT64_REAL, KIND_LOCAL),
    'TInt64::operator+=(TInt64 const &)': ('local', LOCAL_TINT64_ADD, KIND_LOCAL),
    'TInt64::operator-(TInt64 const &) const': ('local', LOCAL_TINT64_SUB, KIND_LOCAL),   # hidden result pointer
    'TInt64::operator*(TInt64 const &) const': ('local', LOCAL_TINT64_MUL, KIND_LOCAL),
    'TInt64::operator/(TInt64 const &) const': ('local', LOCAL_TINT64_DIV, KIND_LOCAL),
    'TInt64::operator>=(TInt64 const &) const': ('local', LOCAL_TINT64_GE, KIND_LOCAL),
    'TInt64::operator<(TInt64 const &) const': ('local', LOCAL_TINT64_LT, KIND_LOCAL),
    # euser's names are the 7.0 def's spellings ('(void)', 'const &'); the
    # rest come out of gnuv2, or are keyed raw where it cannot read them.
    # TRealX is laid out the same in both eras (e32math.h: iMantLo, iMantHi,
    # iSign, iFlag, iExp) and every TRealX the game holds was made by 9.x.
    'TRealX::operator unsigned int(void)': ('euser', 'TRealX::operator unsigned int() const', KIND_CALL),
    '__vc__C4CDiri': ('efsrv', 'CDir::operator[](int) const', KIND_CALL),
    'FFSSpaceBelowCriticalLevelL__SysUtilPRFsi':
        ('sysutil', 'SysUtil::FFSSpaceBelowCriticalLevelL(RFs*, int)', KIND_CALL),
    'PlpVariant::GetMachineIdL(TBuf<128> &)':
        ('plpvariant', 'PlpVariant::GetMachineIdL(TBuf<128>&)', KIND_CALL),
    'CnvUtfConverter::ConvertToUnicodeFromUtf8(TDes16 &, const TDesC8 &)':
        ('charconv', 'CnvUtfConverter::ConvertToUnicodeFromUtf8(TDes16&, TDesC8 const&)', KIND_CALL),
    # EKA2 has no RSemaphore::Count. One's only use (0x1290c) is
    # Signal(-Count()) under a mutex -- wake whoever waits; a count of 0
    # makes that Signal(0), which EKA2 accepts and does nothing with.
    'RSemaphore::Count(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    # estor names with a class passed by value, which gnuv2 cannot read.
    'DoReadL__10TStreamBufR12MStreamInputG15TStreamTransfer':
        ('estor', 'TStreamBuf::DoReadL(MStreamInput&, TStreamTransfer)', KIND_CALL),
    'DoWriteL__10TStreamBufR13MStreamOutputG15TStreamTransfer':
        ('estor', 'TStreamBuf::DoWriteL(MStreamOutput&, TStreamTransfer)', KIND_CALL),
    'ReadL__11RReadStreamR6TDes16G5TChar':
        ('estor', 'RReadStream::ReadL(TDes16&, TChar)', KIND_CALL),
}


# Libraries that exist only in the N-Gage ROM. Nothing on an S60v3 phone
# answers any of them, and between them they are the N-Gage's multiplayer and
# arena services -- a subsystem, not a handful of calls. They all get the same
# stand-in: do nothing, return zero. GCC98r2 call sites keep the pointer they
# allocated rather than the one a constructor returns, so a constructor losing
# its return value costs nothing; what this does cost is that anything built
# this way stays empty, which is the point. Single player is what is wanted
# first. Individual ordinals can be answered properly in BY_ORDINAL.
# arenafoundation is the Arena client Ashen links (online play); sysagt is the
# System Agent, which 9.x removed outright -- its state queries answer 0 and
# its event subscriptions never fire, which a game treats as 'nothing changed'.
NGAGE_ONLY = ('arenaframework', 'arenafoundation', 'bluetooth.dll', 'gamecomms',
              'gameutils', 'nokiafc', 'sysagt')

# Imports with no signature to match on: the N-Gage-only libraries, and the
# exports Nokia added past the end of what any list of ours covers. Each entry
# is a deliberate stand-in, decided from what the game's code does with the
# call and from reading the function itself out of the N-Gage ROM.
BY_ORDINAL = {
    # zlib's `uncompress`, old ezlib 65. The matcher will not pair it by name:
    # the old side carries a full signature and the 9.x side is a plain C
    # symbol, so `uncompress(unsigned char *, unsigned long *, const unsigned
    # char *, unsigned long)` never lines up with `uncompress`. It is the same
    # function with the same four arguments in the same order.
    #
    # 9.x really does ship it: `ezlib.dll` is in the RM-409 ROM with 86
    # exports, and both the Symbian release's `ezlib2u.def` and EKA2L1's
    # `epoc9.def` -- which agrees with the ROM on the count -- put
    # `uncompress` at ordinal 69. Asphalt 1 dies on this import at E280
    # (`G6IMP 393363`, which is 393 imports and index 363) the moment it has
    # opened its data file.
    ('ezlib', 65): ('ezlib', 'uncompress', KIND_CALL),
    # The messaging session the game opens at startup -- Asphalt 1 has a
    # send-your-score feature. Both survive into 9.x under the same names and
    # E284 wired them up: the server came up, and then msgs called back into
    # the MMsvSessionObserver the game handed it, through a GCC98r2 vtable,
    # and landed on a virtual-base delta word. Shimming an observer interface
    # for a feature nobody needs to drive a car is not the trade. LOCAL_NOOP
    # returns zero, which is a session the game never gets.
    ('msgs', 143): ('local', LOCAL_NOOP, KIND_LOCAL),
    ('msgs', 29): ('local', LOCAL_NOOP, KIND_LOCAL),
    # RSessionBase::CreateSession, old euser 285. It does have a 9.x export and
    # forwarding to it is exactly what must not happen: there is no kernel
    # server called SoundServer, because our CServer::StartL is a stand-in that
    # registers nothing, so the real call answers KErrNotFound.
    #
    # 0xb86ec is the classic connect-or-start-the-server idiom -- it builds the
    # name "SoundServer", tries to connect, starts a thread with the 100000-byte
    # stack the caller passes in, tries again, and returns KErrNotFound when
    # both fail. 0xba500 calls it and LeaveIfErrors the answer, and that leave
    # is where every run has ended since E115.
    #
    # KErrNone, to agree with SendReceive, which already answers KErrNone. The
    # game believes it has a sound session and every request it makes is
    # swallowed. Sound is silent; the game gets on with it. The honest version
    # is the in-process bridge -- both ends of this server are in our own
    # address space -- and this is the placeholder that keeps the run moving
    # until audio is worth building.
    ('euser', 285): ('local', LOCAL_NOOP, KIND_LOCAL),
    # ConstructL localises "Invalid game card" into five languages and hands it,
    # with the ASCII name "N-Gage", to this. Nothing on an S60v3 phone would
    # ever show that message, so it succeeds and does nothing.
    ('nokiafc', 1): ('local', LOCAL_NOOP, KIND_LOCAL),
    # A Nokia addition past cone's ordinary exports -- in the ROM it copies a
    # size, compares it against something keyed by UID 0x101f8a5a and a factor
    # of 0.4, and adjusts. The game calls it on its control with the rectangle
    # ApplicationRect just gave it, immediately after CreateWindowL, which is
    # where SetRect goes; 9.x has no counterpart, so SetRect is the stand-in.
    ('cone', 318): ('cone', 'CCoeControl::SetRect(const TRect&)', KIND_CALL),
    # Direct screen access, which is what a game wants the screen for. In the
    # ROM, 348 takes four arguments and returns a 0x60-byte object, and 350
    # starts one off; the game hands the first the window server session and
    # screen device it read out of its CCoeEnv, its window, and itself as the
    # abort observer. 9.x still has both, under their own names.
    ('ws32', 348): ('ws32', 'CDirectScreenAccess::NewL(RWsSession&, CWsScreenDevice&,'
                            ' RWindowBase&, MDirectScreenAccess&)', KIND_CALL),
    ('ws32', 350): ('ws32', 'CDirectScreenAccess::StartL()', KIND_CALL),
    # Key click sounds. CAknAppUi::KeySounds() is inline in 9.x and reads a
    # field of CAknAppUiBase, which our app UI -- a CEikAppUi -- does not have,
    # so it answers with nothing and the context push it feeds does nothing
    # either. Cosmetic, and the pair has to be stubbed together: the second is
    # called on whatever the first returns.
    ('avkon', 874): ('local', LOCAL_NOOP, KIND_LOCAL),
    ('avkon', 1272): ('local', LOCAL_NOOP, KIND_LOCAL),
    # And the third of that family. CAknAppUi::SetKeyBlockMode is not virtual
    # and writes CAknAppUiBase's own members, which the game's app UI does not
    # have: avkon writes at its own offsets into an object that is not one of
    # its, and what it lands on depends on where the heap put things. It was
    # found setting our app UI's vtable pointer to 1 -- a TBool at avkon's
    # offset, the vptr at ours -- which the framework then dispatched
    # ProcessCommandParametersL through. It decides whether holding a key
    # repeats it. The key here is the ordinal in the N-Gage's avkon, not the
    # 9.x one this used to name, which is why it went on being called for real.
    ('avkon', 1529): ('local', LOCAL_NOOP, KIND_LOCAL),
    # CEikAppUi::IsForeground(), in all but name. In the N-Gage ROM it checks
    # that it is still the CCoeEnv's app UI and that the window server's
    # focused window group belongs to this process. The game asks it, together
    # with CCoeControl::IsFocused(), before it will start its frame loop.
    # Nothing exports it on 9.x, and the focus half already answers honestly:
    # cone only focuses a stacked control while its application has the
    # foreground, so the foreground half is true whenever it is asked.
    ('eikcore', 286): ('local', LOCAL_TRUE, KIND_LOCAL),
    # Ashen, round 109. bitgdi's last exports are past epoc6's list; the N-Gage
    # ROM's 170 is a one-argument NewL (push cleanup, new, ConstructL, pop) and
    # the game hands it the CFbsBitmap it has just created in EColor4K and then
    # asks the result for a CreateContext -- CFbsBitmapDevice::NewL(CFbsBitmap*).
    ('bitgdi', 170): ('bitgdi', 'CFbsBitmapDevice::NewL(CFbsBitmap*)', KIND_CALL),
    # The System Agent, which 9.x removed. A whole-library no-op is not enough
    # for an event watcher (E367): NotifyOnEvent that leaves the request
    # status at 0 makes a CActive that is 'active and complete' forever, and
    # the scheduler dispatches it on every wake ahead of everything below its
    # priority -- the game's own loop never ran. These three keep the contract
    # of an agent with no events: the status goes pending and stays so, and a
    # cancel completes it with KErrCancel for CActive::Cancel to collect.
    ('sysagt', 12): ('local', LOCAL_SYSAGT_SETSTATUS, KIND_LOCAL),   # TSysAgentEvent::SetRequestStatus(TRequestStatus&)
    ('sysagt', 8): ('local', LOCAL_SYSAGT_NOTIFY, KIND_LOCAL),       # RSystemAgent::NotifyOnEvent(TSysAgentEvent&)
    ('sysagt', 4): ('local', LOCAL_SYSAGT_CANCEL, KIND_LOCAL),       # RSystemAgent::NotifyEventCancel()
    # One, round 125. Three N-Gage additions past cone's epoc6 list, read
    # out of the RH-29 ROM (cone.dll export directory): 315 is
    # `str r1, [r0, #0x2c]; bx lr`, 312 is `ldr r0, [r0, #0x2c]; bx lr`,
    # 314 is `movs r0, #0; bx lr`. The game calls 315 in its app UI's
    # ConstructL on its freshly built container with the app UI's
    # MObjectProvider sub-object (this+0x1c) -- the `SetMopParent(this)`
    # idiom -- so the word is the old-layout control's MOP parent, and the
    # stand-ins do to the game's object exactly what the ROM did.
    ('cone', 315): ('local', LOCAL_SET_WORD_2C, KIND_LOCAL),
    ('cone', 312): ('local', LOCAL_GET_WORD_2C, KIND_LOCAL),
    ('cone', 314): ('local', LOCAL_NOOP, KIND_LOCAL),
    # RFs::SetDefaultPath, old efsrv 168. 9.x keeps the export and panics the
    # caller with it (cl_insecure.cpp: 'FSInsecCli panic' 1, E468): the
    # process-wide default path went with platform security. A no-op (E470)
    # left the game's drive-less file names resolving against C:\ (E474), so
    # it is a hook: IMPORT_FS_SET_DEFAULT_PATH keeps the path and
    # IMPORT_FS_CONNECT sets it on every later session. By ordinal, not
    # MANUAL: a name that matches a 9.x export directly is forwarded before
    # MANUAL is consulted (E469).
    ('efsrv', 168): ('local', LOCAL_NOOP, KIND_LOCAL),
    # TEntry and TVolumeInfo are bigger on 9.x than the game lays them out
    # (f32file.h: two words after TEntry's name; TDriveInfo's bus type and
    # the cache flags in TVolumeInfo), and both constructors write the new
    # words -- past the game's object, onto its saved registers (E471).
    # TEntry's sets nothing else, so nothing; TVolumeInfo's zero-fills, so a
    # local zeroes the old size. RFs::Entry and RFs::Volume are hooks
    # (IMPORT_FS_ENTRY, IMPORT_FS_VOLUME) that fill a buffer and copy back.
    ('efsrv', 227): ('local', LOCAL_SELF, KIND_LOCAL),              # TEntry::TEntry()
    ('efsrv', 212): ('local', LOCAL_VOLUMEINFO_CTOR, KIND_LOCAL),   # TVolumeInfo::TVolumeInfo()
}


def fallback_table(sigs):
    """An epoc9.def list -> a def table {ordinal: (name, signature)}.

    epoc9.def keeps the demangler's suffix on some names ('(complete object
    constructor)', '(base object constructor)', on a few non-constructors
    too); norm() cannot parse it, so it goes. The name column is the
    signature itself: llvm-cxxfilt hands a non-symbol back unchanged.
    """
    table = {}
    for o, sig in enumerate(sigs, 1):
        sig = sig.replace(' (complete object constructor)', '').replace(' (base object constructor)', '')
        table[o] = (sig, sig)
    return table


def _is_framework_ctor(sig):
    """Class::Class(...) or Class::~Class(...) for a class the game derives from."""
    m = re.match(r'^(C\w+)::(~?)\1\s*\(', sig)
    return bool(m) and m.group(1) in FRAMEWORK_BASES


# WS322U.DEF is ws32.dll -- the digit is the def's own version, not part of the
# library's name, and nothing in the file says so.
DEF_ALIASES = {'ws32': 'ws322'}


def find_defs():
    out = {}
    for dirpath, _dirs, files in os.walk(ROOT):
        if 'eabi' not in dirpath.lower():
            continue
        for f in files:
            if not f.lower().endswith('.def'):
                continue
            b = symdef.base_name(f)
            for key in {b, b.rstrip('u'), re.sub(r'\d+u?$', '', b)}:
                out.setdefault(key, os.path.join(dirpath, f))
    for name, actual in DEF_ALIASES.items():
        if actual in out:
            out.setdefault(name, out[actual])
    return out



# ---------------------------------------------------------------------------
# The hooks, keyed by what they are rather than where they sit.
#
# Every diversion the port installs used to be a hardcoded import index --
# `IMPORT_DSA_NEWL = 460`, `IMPORT_SCREEN_UPDATE = 47`. Those are positions in
# **Asphalt 2's** import table and mean something else in every other game, so
# they were the one thing standing between this layer and a second title.
#
# An import is identified by the DLL it comes from and the ordinal it asks
# for, and that pair is the same in any game that calls the same function. So
# name the pair here and let the generator find the index in whatever image it
# is pointed at. A hook the image does not import is emitted as HOOK_ABSENT,
# which is past any real table, so the `nImports > IMPORT_X` guards already in
# the source decline to install it.
HOOK_ABSENT = 0xFFFF

HOOKS = {
    'IMPORT_DLL_NAME':                ('apparc', 13),
    'IMPORT_APP_FULL_NAME':           ('apparc', 3),    # CApaApplication::AppFullName(): the same answer as DllName -- One takes its data path from it (round 125)
    'IMPORT_BASECONSTRUCTL':          ('avkon', 63),
    'IMPORT_SET_AUTO_UPDATE':         ('bitgdi', 105),
    'IMPORT_GC_SETCLIP':              ('bitgdi', 111),  # CFbsBitGc::SetClippingRegion(const TRegion*) -- wrapped after the divert, logged (round 157)
    'IMPORT_SCREEN_UPDATE':           ('bitgdi', 137),
    'IMPORT_ADD_FOREGROUND_OBSERVER': ('cone', 8),
    'IMPORT_ADDTOSTACKL':             ('cone', 12),
    'IMPORT_CREATEWINDOWL':           ('cone', 40),
    'IMPORT_COEENV_STATIC':           ('cone', 223),
    'IMPORT_SYSTEM_GC':               ('cone', 226),    # CCoeControl::SystemGc() const -- answered from the real environment
    'IMPORT_CREATE_CONTEXT':          ('bitgdi', 23),   # CFbsDevice::CreateContext(CFbsBitGc*&) -- the result wrapped in the old-shaped stand-in (E373)
    'IMPORT_CLEANUP_NEW':             ('euser', 746),   # CTrapCleanup::New() -- a stand-in the game's old-ABI delete can reach (E381)
    'IMPORT_FILE_READ_POS':           ('efsrv', 142),   # RFile::Read(TInt, TDes8&, TInt) -- logged: round 111's pack read on the N95
    'IMPORT_FIND_WILD_BY_DIR':        ('efsrv', 46),    # TFindFile::FindWildByDir: the CDir it hands back made deletable the old way (round 125)
    'IMPORT_FILEMAN_NEWL':            ('efsrv', 106),   # CFileMan::NewL(RFs&): the same
    'IMPORT_FS_ENTRY':                ('efsrv', 39),    # RFs::Entry(const TDesC&, TEntry&): filled at the old size (round 125)
    'IMPORT_FS_VOLUME':               ('efsrv', 193),   # RFs::Volume(TVolumeInfo&, TInt): the same, field by field
    'IMPORT_FS_SET_DEFAULT_PATH':     ('efsrv', 168),   # RFs::SetDefaultPath: kept, and set as the session path (round 125)
    'IMPORT_FS_CONNECT':              ('efsrv', 18),    # RFs::Connect: the new session given the default path
    'IMPORT_PUSHL_CBASE':             ('euser', 858),   # CleanupStack::PushL(CBase*): the game's own objects pushed with their old destructor (round 125)
    'IMPORT_FILE_SIZE':               ('efsrv', 185),   # RFile::Size(TInt&) -- logged, the same
    # Round 113: GCC98r2 keeps a double's high word first (the FPA order) and
    # EABI keeps it last, so every double that crosses to a 9.x helper or a
    # Math function has its words swapped on the way in and out (gate6.cpp,
    # dswap_thunk and the gate6_fpa_* handlers). The old euser ordinals.
    'IMPORT_ADDDF3':                  ('euser', 1454),
    'IMPORT_SUBDF3':                  ('euser', 1613),
    'IMPORT_MULDF3':                  ('euser', 1570),
    'IMPORT_DIVDF3':                  ('euser', 1512),
    'IMPORT_NEGDF2':                  ('euser', 1579),
    'IMPORT_FLOATSIDF':               ('euser', 1528),
    'IMPORT_EXTENDSFDF2':             ('euser', 1525),
    'IMPORT_FIXDFSI':                 ('euser', 1526),
    'IMPORT_TRUNCDFSF2':              ('euser', 1615),
    'IMPORT_LTDF2':                   ('euser', 1541),
    'IMPORT_GTDF2':                   ('euser', 1534),
    'IMPORT_GEDF2':                   ('euser', 1531),
    'IMPORT_LEDF2':                   ('euser', 1537),
    'IMPORT_EQDF2':                   ('euser', 1523),
    'IMPORT_NEDF2':                   ('euser', 1578),
    'IMPORT_MATH_POW':                ('euser', 834),
    'IMPORT_MATH_MOD':                ('euser', 703),
    'IMPORT_MATH_ATAN2':              ('euser', 6),
    'IMPORT_MATH_SIN':                ('euser', 1082),
    'IMPORT_MATH_COS':                ('euser', 254),
    'IMPORT_MATH_TAN':                ('euser', 1131),
    'IMPORT_MATH_SQRT':               ('euser', 1101),
    'IMPORT_MATH_EXP':                ('euser', 398),
    'IMPORT_MATH_LN':                 ('euser', 637),
    'IMPORT_MATH_LOG':                ('euser', 662),
    'IMPORT_MATH_FRAC':               ('euser', 468),
    'IMPORT_MATH_ASIN':               ('euser', 4),
    'IMPORT_MATH_ACOS':               ('euser', 1622),
    'IMPORT_MATH_ATAN':               ('euser', 5),
    'IMPORT_MATH_INT_D':              ('euser', 570),   # Math::Int(TReal&, const TReal&)
    'IMPORT_MATH_INT':                ('euser', 571),   # Math::Int(TInt32&, const TReal&)
    'IMPORT_MATH_INT16':              ('euser', 572),   # Math::Int(TInt16&, const TReal&)
    'IMPORT_MATH_ROUND':              ('euser', 961),   # Math::Round(TReal&, const TReal&, TInt)
    'IMPORT_MATH_FRAND':              ('euser', 404),   # Math::FRand(TInt64&): a double comes back in r0:r1 (One, round 125)
    'IMPORT_LEX16_VAL_REAL':          ('euser', 1199),  # TLex16::Val(TReal64&): 9.x writes the double in EABI word order, the game reads FPA order (One, round 144)
    'IMPORT_CTL_RECT':                ('cone', 160),    # CCoeControl::Rect() -- answered 176x208 while the wrapper is the whole screen (round 114)
    'IMPORT_CTL_SETRECT':             ('cone', 318),    # the Nokia SetRect export -- the wrapper is sized to the whole screen instead (round 114)
    'IMPORT_DES_FORMAT':              ('euser', 467),   # TDes16::Format(fmt, ...) -- logged under GAME_LOG_TEXT (round 116)
    'IMPORT_DES_FORMATLIST':          ('euser', 463),   # TDes16::FormatList(fmt, VA_LIST) -- the GCC98r2 array address re-pointed to its element for the EABI euser (round 116)
    'IMPORT_CTL_DRAWABLE_WINDOW':     ('cone', 55),     # CCoeControl::DrawableWindow() const -- a candidate for the wrapper's window, validated against the session buffer (round 120)
    'IMPORT_APPUI_EXIT':              ('eikcore', 67),  # CEikAppUi::Exit() -- on 9.x a leave the framework catches; through the game's frames it is a terminate (round 113)
    'IMPORT_APPUI_PREPARE_EXIT':      ('avkon', 1254),  # CAknAppUi::PrepareToExit() -- on the game's own old-layout app UI it reads a member that is not there; answered with nothing (round 149)
    'IMPORT_FBS_BITMAP_CTOR':         ('fbscli', 135),  # CFbsBitmap::CFbsBitmap() -- the port's scaler makes its own bitmap with the game's entries (round 150)
    'IMPORT_FBS_BITMAP_CREATE':       ('fbscli', 16),   # CFbsBitmap::Create(const TSize&, TDisplayMode)
    'IMPORT_FBS_DATA_ADDRESS':        ('fbscli', 17),   # CFbsBitmap::DataAddress() const
    'IMPORT_FBS_DISPLAY_MODE':        ('fbscli', 21),   # CFbsBitmap::DisplayMode() const
    'IMPORT_READ_FILE_SECTION':       ('efsrv', 129),   # RFs::ReadFileSection(name, pos, des, len) -- a missing `\Game.Id` answered with the card's six bytes (round 112)
    'IMPORT_WINDOW':                  ('cone', 231),
    'IMPORT_COECONTROL_CTOR':         ('cone', 236),
    'IMPORT_FILE_CREATE':             ('efsrv', 25),
    'IMPORT_FILE_OPEN':               ('efsrv', 121),
    'IMPORT_FILE_READ':               ('efsrv', 136),
    'IMPORT_FILE_REPLACE':            ('efsrv', 151),
    # Every route the game has to the heap, so an allocation can be padded:
    # a 9.x framework object constructed into an EKA1-sized cell writes
    # past the end of it (E277).
    'IMPORT_USER_ALLOCZL':            ('euser', 3),
    'IMPORT_USER_ALLOCL':             ('euser', 35),
    'IMPORT_USER_ALLOC':              ('euser', 45),
    'IMPORT_USER_ALLOCZ':             ('euser', 1582),
    'IMPORT_MALLOC':                  ('estlib', 3),
    'IMPORT_CANCEL':                  ('euser', 152),
    'IMPORT_MSG_COMPLETE':            ('euser', 209),
    'IMPORT_SEM_CREATE':              ('euser', 280),
    'IMPORT_CREATE_SESSION':          ('euser', 285),
    'IMPORT_THREAD_CREATE':           ('euser', 289),
    # RThread::Open, so a create that comes back KErrAlreadyExists can be
    # turned into a handle on the thread that is already there. Round 90.
    'IMPORT_THREAD_OPEN':             ('euser', 803),
    'IMPORT_EXIT':                    ('euser', 397),
    'IMPORT_USER_FREE_OP':            ('euser', 476),
    'IMPORT_LEAVE_IF_ERROR':          ('euser', 627),
    'IMPORT_LEAVE_NOMEM':             ('euser', 629),
    'IMPORT_LEAVE':                   ('euser', 630),
    'IMPORT_LIBRARY_LOAD':            ('euser', 644),
    'IMPORT_LIBRARY_LOOKUP':          ('euser', 672),
    'IMPORT_PANIC':                   ('euser', 809),
    'IMPORT_REQUEST_COMPLETE':        ('euser', 926),
    'IMPORT_SCREEN_INFO':             ('euser', 970),
    'IMPORT_SEND_RECEIVE':            ('euser', 972),
    'IMPORT_SERVER_STARTL':           ('euser', 1102),
    'IMPORT_SEM_WAIT':                ('euser', 1212),
    'IMPORT_CTIMER_CTOR':             ('euser', 1377),
    'IMPORT_CTIMER_DTOR':             ('euser', 1264),   # CTimer::~CTimer(): sent to the stand-in (GAME_MULTI_TIMER, round 125)
    'IMPORT_CTIMER_DOCANCEL':         ('euser', 350),    # CTimer::DoCancel(): the same
    'IMPORT_CACTIVE_CTOR':            ('euser', 1395),   # CActive::CActive(TInt): GAME_AO_PRIORITIES (One, round 128)
    'IMPORT_THREAD_SUSPEND':          ('euser', 1122),   # RThread::Suspend(): refused on an impossible handle (One, round 130)
    'IMPORT_THREAD_RESUME':           ('euser', 954),   # RThread::Resume(): the same
    # RWindow is 8 bytes on EKA1 and 24 on 9.x (RDrawableWindow's draw rect at
    # +8): these four write the extra 16 and are run on a full-size copy
    # (Colin McRae, E890: the constructor zeroed the engine's saved registers).
    'IMPORT_RWIN_CTOR':               ('ws32', 283),    # RWindow::RWindow()
    'IMPORT_RWIN_CTOR_WS':            ('ws32', 284),    # RWindow::RWindow(RWsSession &)
    'IMPORT_RWIN_BEGIN_REDRAW':       ('ws32', 11),     # RWindow::BeginRedraw(const TRect &)
    'IMPORT_RWIN_END_REDRAW':         ('ws32', 103),    # RWindow::EndRedraw()
    'IMPORT_DEF_MODE_COLORS':         ('ws32', 300),    # RWsSession::GetDefModeMaxNumColors: the N-Gage's EColor4K for a 16-bit title (Colin McRae, E892)
    'IMPORT_ADD_EVENT':               ('euser', 9),     # UserSvr::AddEvent: an AirPlay engine's ERedraw is its frame done (Colin McRae, E908)
    'IMPORT_START_APP':               ('apgrfx', 96),   # RApaLsSession::StartApp: an AirPlay engine starts its front end, whose port is another loader (Colin McRae, E959)
    'IMPORT_CHUNK_CREATE_GLOBAL':     ('euser', 275),   # RChunk::CreateGlobal: the port makes an AirPlay engine's I3D shared memory, as its launcher did (Colin McRae, E914)
    'IMPORT_CREATE_BITMAP':           ('eikcore', 44),  # CEikonEnv::CreateBitmapL: "*" is the game's own .mbm, not the loader's (Colin McRae's front end, E952)
    'IMPORT_AKN_CBA':                 ('avkon', 251),   # CAknAppUi::Cba(): a hidden stand-in (the port's app UI has no screen furniture; E956)
    'IMPORT_AKN_STATUS_PANE':         ('avkon', 1816),  # CAknAppUi::StatusPane(): likewise
    # ROM objects the game is handed, given a shadow vtable whose slot +8 takes
    # a GCC 2.x delete (E957; gate6_shadow). Factories of four arguments or fewer.
    'IMPORT_SHADOW_PERIODIC':         ('euser', 738),   # CPeriodic::NewL(int)
    'IMPORT_SHADOW_BUFFLAT':          ('euser', 733),   # CBufFlat::NewL(int)
    'IMPORT_SHADOW_DESC8FLAT':        ('bafl', 162),    # CDesC8ArrayFlat::CDesC8ArrayFlat(int)
    'IMPORT_SHADOW_DESC16FLAT':       ('bafl', 166),    # CDesC16ArrayFlat::CDesC16ArrayFlat(int)
    'IMPORT_HANDLE_CLOSE':            ('euser', 172),   # RHandleBase::Close(): the handle word logged before the call (round 136: a worker's Close died KERN-EXEC 0 on the N95)
    'IMPORT_DELETE_OP':               ('euser', 1504),
    'IMPORT_VEC_DELETE_OP':           ('euser', 1506),
    'IMPORT_APP_RECT':                ('eikcore', 233),
    'IMPORT_APPUI_FACTORY':           ('eikcore', 16),    # CEikonEnv::AppUiFactory(): a null-object factory, since the furniture it would name is never built (One, round 125)
    'IMPORT_CLIENT_RECT':             ('eikcore', 32),    # CEikAppUi::ClientRect(): a divert on r1 (DIVERTS); named here so the r0 app UI thunk leaves it alone (One, round 125)
    'IMPORT_MDA_NEWL':                ('mediaclientaudiostream', 2),
    'IMPORT_DSA_NEWL':                ('ws32', 348),
    'IMPORT_DSA_STARTL':              ('ws32', 350),
}
# **The trace lists, keyed the same way as the hooks.**
#
# Both of these used to be arrays of import indices written down in gate6.cpp
# while looking at Asphalt 2. An index names a different function in every
# other game, so in Asphalt 1 the milestone list traced `__floatsisf` 32,768
# times (E278) and traced none of the things it is for. Keyed by (DLL, old
# ordinal) they are right in any title, and an import the game does not have
# simply drops out.
#
# **The diversions, keyed the same way.** Same trap as the trace lists, and a
# worse one: these were nine Asphalt 2 import indices, so in any other game
# they diverted nine unrelated functions and left the nine that matter alone.
# E281 died because of it -- the game called `CCoeControl::ActivateL()` on its
# own EKA1 control, cone read a member at +0x2c that is not there, and the
# divert that would have swapped in the wrapper was sitting on index 49, which
# is not ActivateL in this game.
#
# Each entry is (DLL, old ordinal, which register holds the object, which
# wrapper). The register is 1 only where the method returns a structure, which
# pushes `this` along one.
ON_APP_UI, ON_CONTROL, ON_TIMER, ON_GC = 0, 1, 2, 3

DIVERTS = [
    ('eikcore', 233, 1, ON_APP_UI),   # CEikAppUi::ApplicationRect() const
    ('eikcore', 32, 1, ON_APP_UI),    # CEikAppUi::ClientRect() const -- a TRect, so `this` is r1 (One, round 125; E483 took the r0 thunk)
    ('cone', 318, 0, ON_CONTROL),     # the Nokia export standing in for SetRect
    ('cone', 3, 0, ON_CONTROL),       # CCoeControl::ActivateL()
    ('cone', 114, 0, ON_CONTROL),     # CCoeControl::IsFocused() const
    ('cone', 201, 0, ON_CONTROL),     # CCoeControl::SetExtentToWholeScreen() -- One's container, right after CreateWindowL (E486: cone read 0x48 off the old object)
    # One, round 159: its one DrawNow, on its own control at the first frame.
    # S60 3.0's cone keeps a control's flags behind a pointer at +0x2c (3.1
    # and 3.2: a plain word at +0x24); the old object has a zero there, and
    # the read 8 past it is the N73's death (E878 on the N80 firmware: pc and
    # lr at the same offsets from the DrawNow entry as the N73's).
    ('cone', 53, 0, ON_CONTROL),      # CCoeControl::DrawNow() const
    # Ashen, round 109: a title that draws through its control rather than
    # direct screen access calls these on its own old-layout control, and
    # cone reads 9.x offsets off it (E363: SystemGc walked to iCoeEnv at +0xc,
    # found 0, read 0x40). The map sends them to the wrapper control. SystemGc
    # itself is a hook (IMPORT_SYSTEM_GC): Ashen calls it on its container
    # before CreateWindowL, when there is no wrapper yet to map to.
    ('cone', 55, 0, ON_CONTROL),      # CCoeControl::DrawableWindow() const
    ('cone', 160, 1, ON_CONTROL),     # CCoeControl::Rect() const -- TRect is returned through r0, `this` is r1
    ('cone', 138, 0, ON_CONTROL),     # CCoeControl::MakeVisible(TBool)
    ('cone', 197, 1, ON_CONTROL),     # CCoeControl::SetContainerWindowL(const CCoeControl&): the container argument
    ('cone', 167, 1, ON_CONTROL),     # CCoeAppUi::RemoveFromStack(CCoeControl*): the control argument
    # Ashen, round 109: the game's own app UI overrides HandleForegroundEventL
    # and HandleSystemEventL and calls the avkon base from each, on its
    # old-layout object. The wrapper forwards those slots to the game
    # (GAME_UI_FORWARD_EVENTS); the base calls come back here and go to the
    # wrapper, which is the object avkon can read. The names are epoc6 leads
    # on a mismatched library, corroborated by the override that calls each
    # one with its own arguments (ROUNDS E368).
    ('avkon', 757, 0, ON_APP_UI),     # CAknAppUi::HandleForegroundEventL(TBool)
    ('avkon', 803, 0, ON_APP_UI),     # CAknAppUi::HandleSystemEventL(const TWsEvent&)
    ('bitgdi', 111, 0, ON_GC),        # CFbsBitGc::SetClippingRegion(const TRegion *)
    ('euser', 224, 0, ON_TIMER),      # CTimer::ConstructL()
    ('euser', 22, 0, ON_TIMER),       # CTimer::After(TTimeIntervalMicroSeconds32)
    ('euser', 13, 0, ON_TIMER),       # CActiveScheduler::Add(CActive *)
    ('euser', 979, 0, ON_TIMER),      # CActive::SetActive()
]

# MILESTONES: files, libraries, the screen's one-shot setup, the thread and
# semaphore handshake, the server session, and the sound path. Deliberately
# not the per-frame ones -- see the note in gate6.cpp about flushing to a
# memory card.
MILESTONES = [
    # A window-server client's own windows (Colin McRae's engine, E898):
    # group and window construction, extent, activation, ordinal positions,
    # the default mode, the DSA's NewL/StartL.
    ('ws32', 46), ('ws32', 51), ('ws32', 206), ('ws32', 291), ('ws32', 211),
    ('ws32', 239), ('ws32', 300), ('ws32', 348), ('ws32', 350),
    ('efsrv', 18), ('efsrv', 121), ('efsrv', 136), ('efsrv', 15),
    ('euser', 644), ('euser', 672), ('euser', 180),
    ('bitgdi', 105), ('bitgdi', 111), ('euser', 926),
    ('euser', 280), ('euser', 289), ('euser', 1016), ('euser', 954),
    ('euser', 1212), ('euser', 172),
    ('euser', 746), ('euser', 1343), ('euser', 569), ('euser', 1078),
    ('euser', 627),   # User::LeaveIfError -- bench instrument for round 113's leave -1003
    ('euser', 1107), ('euser', 285),
    ('euser', 972), ('euser', 279), ('euser', 1398), ('euser', 1102),
    ('euser', 966), ('euser', 1414), ('euser', 209), ('euser', 1226),
    ('euser', 814), ('mediaclientaudiostream', 2),
    # Not ('euser', 1148), ('euser', 35), ('euser', 1164): TTrap::Trap,
    # User::AllocL and TTrap::UnTrap. One calls them every frame, and they
    # were 37,000 of round 126's 131,000 records.
    ('euser', 629),
    ('euser', 476),
    # Round 147 (Ashen): the save path. The N95 showed Opens of the save
    # slots and nothing of what created one, because Replace, Write, Flush,
    # MkDir and Delete were never traced.
    ('efsrv', 151), ('efsrv', 200), ('efsrv', 50), ('efsrv', 86), ('efsrv', 29),
]

# HOT: the ones that fire thousands of times a second and say nothing.
HOT = [
    ('euser', 1616), ('euser', 1617), ('euser', 99), ('euser', 243),
    ('euser', 89), ('euser', 1569), ('euser', 1283), ('euser', 1114),
    ('euser', 1366), ('euser', 2), ('euser', 856),
]

# `RFile::Read` is diverted twice, once as the wrapped call and once as the
# entry the static import table still holds. One ordinal, two names.
HOOK_ALIASES = {'IMPORT_FILE_READ_STATIC': 'IMPORT_FILE_READ'}


def hook_indices(imports):
    """{name: index} for every hook, HOOK_ABSENT where the game has no such import."""
    where = {}
    for i, (lib, o) in enumerate(imports):
        where.setdefault((lib, o), i)
    out = {name: where.get(key, HOOK_ABSENT) for name, key in HOOKS.items()}
    for alias, of in HOOK_ALIASES.items():
        out[alias] = out[of]
    return out


def divert_indices(imports):
    """[(index, register, wrapper)] for the diversions this game imports."""
    where = {}
    for i, (lib, o) in enumerate(imports):
        where.setdefault((lib, o), i)
    out = []
    for lib, o, reg, obj in DIVERTS:
        i = where.get((lib, o))
        if i is not None:
            out.append((i, reg, obj))
    return out


def list_indices(imports, wanted):
    """The import indices of a (DLL, old ordinal) list, in order, skipping the
    ones this game does not import."""
    where = {}
    for i, (lib, o) in enumerate(imports):
        where.setdefault((lib, o), i)
    out = []
    for key in wanted:
        i = where.get(key)
        if i is not None and i not in out:
            out.append(i)
    return out


def coeenv_methods(imports, db, hooked=()):
    """Import indices that are a method **on** CCoeEnv.

    The game is handed `coeEnvView`, a copy of the real environment laid out
    the old way so its field reads land right. That works until it calls a
    method on it: cone then runs with `this` pointing at the copy. These are
    the entries where that happens, so the loader can put the real pointer
    back. `Static()` is excluded -- it takes no `this` and is hooked already.
    """
    # CEikonEnv derives from CCoeEnv and is the same object; its eikcore
    # methods read the derived part, which the view does not carry at all
    # (E371: `CEikonEnv::DenseFont()` on the view handed uiklaf a null font
    # array). Ashen is the first title to import one; the Asphalts import none.
    out = []
    for i, (lib, o) in enumerate(imports):
        base = symdef.base_name(lib)
        if base not in ('cone', 'eikcore'):
            continue
        table = db.get(base) or []
        if o - 1 >= len(table):
            continue
        raw = table[o - 1]
        try:
            sig = gnuv2.demangle(raw) or raw
        except Exception:
            sig = raw
        if not (sig.startswith('CCoeEnv::') or sig.startswith('CEikonEnv::')):
            continue
        if sig.startswith('CCoeEnv::Static') or sig.startswith('CEikonEnv::Static'):
            continue
        # An import the port already diverts is not ours to wrap: the swap
        # would chain in front of that hook and change what it is handed.
        # Asphalt 2's `CCoeEnv::AddForegroundObserverL` is one of these.
        if i in hooked:
            continue
        out.append((i, sig))
    return out


def appui_methods(imports, db, hooked=()):
    """Import indices that are a method on CEikAppUi.

    E263: the game calls `CEikAppUi::ApplicationRect()` with a null `this`
    and eikcore dereferences it. Whatever it thought it was holding, we have
    a real 9.x app UI, so these are the entries where a null can be replaced
    with it.
    """
    out = []
    for i, (lib, o) in enumerate(imports):
        if symdef.base_name(lib) != 'eikcore':
            continue
        table = db.get('eikcore') or []
        if o - 1 >= len(table):
            continue
        raw = table[o - 1]
        try:
            sig = gnuv2.demangle(raw) or raw
        except Exception:
            sig = raw
        if not sig.startswith('CEikAppUi::') or i in hooked:
            continue
        if '::CEikAppUi(' in sig or '::~' in sig:      # not the constructors
            continue
        out.append((i, sig))
    return out


def emit_hooks(indices, path, image, uid3=0, coeenv=(), appui=(),
               milestones=(), hot=(), diverts=()):
    lines = ['// Generated by gen_shim.py from %s -- do not edit.' % os.path.basename(image),
             '//',
             '// Which import index each diversion sits at in THIS game. See HOOKS in',
             '// gen_shim.py for what each one is; %d means the game never imports it'
             % HOOK_ABSENT,
             '// and the guards in gate6.cpp then leave that hook uninstalled.',
             '//',
             '// GAME_UID3 is the image these indices were read out of. The loader',
             '// checks it against the image it is actually given, because this',
             '// header and gate4_shim.cpp are generated as a pair from one game',
             '// and are meaningless against another -- and a wrong index is not an',
             '// error, it is a call to the wrong function.',
             '#ifndef GATE_IMPORTS_H',
             '#define GATE_IMPORTS_H',
             '',
             'enum { GAME_UID3 = 0x%08x };' % uid3,
             '',
             'enum {']
    for name in sorted(indices):
        v = indices[name]
        lines.append('    %-34s = %5d,%s' % (name, v, '   // absent' if v == HOOK_ABSENT else ''))
    lines += ['};', '']
    # Imports that are methods on CCoeEnv: see coeenv_methods.
    lines += ['// Imports that are a method on CCoeEnv. The game holds a copy of the',
              '// environment laid out the old way; cone has to be given the real one.']
    if coeenv:
        for i, sig in coeenv:
            lines.append('//   %4d  %s' % (i, sig))
        lines.append('#define GATE_COEENV_METHODS  %s'
                     % ', '.join(str(i) for i, _s in coeenv))
    else:
        lines.append('#define GATE_COEENV_METHODS  /* none */')
    lines.append('#define GATE_COEENV_METHOD_COUNT %d' % len(coeenv))
    lines += ['', '// Methods on CEikAppUi. A null `this` from the game is replaced',
              '// with the real app UI, when the game asks for it (GAME_FIX_APPUI_THIS).']
    if appui:
        lines.append('#define GATE_APPUI_METHODS  %s'
                     % ', '.join(str(i) for i, _s in appui))
    else:
        lines.append('#define GATE_APPUI_METHODS  /* none */')
    lines.append('#define GATE_APPUI_METHOD_COUNT %d' % len(appui))
    lines += ['', '// The trace lists: see MILESTONES and HOT in gen_shim.py. Both may be',
              '// empty, which is why gate6.cpp appends the two exits to the first.']
    lines.append('#define GATE_MILESTONES  %s'
                 % (', '.join(str(i) for i in milestones) + ',' if milestones else ''))
    lines.append('#define GATE_HOT  %s'
                 % (', '.join(str(i) for i in hot) + ',' if hot else ''))
    lines.append('#define GATE_HOT_COUNT %d' % len(hot))
    lines += ['', '// The diversions: see DIVERTS in gen_shim.py. {import, register,',
              '// which wrapper}, and an import the game does not have is left out.']
    if diverts:
        lines.append('#define GATE_DIVERTS  %s'
                     % ' '.join('{ %d, %d, %d },' % d for d in diverts))
    else:
        lines.append('#define GATE_DIVERTS  /* none */')
    lines.append('#define GATE_DIVERT_COUNT %d' % len(diverts))
    lines += ['', '#endif']
    open(path, 'w').write('\n'.join(lines) + '\n')
    return sum(1 for v in indices.values() if v != HOOK_ABSENT)


def image_imports(image):
    """(imports, uid3) for an E32 image, or for an AirPlay engine (`.nax`,
    or the inflated `.lxe` lxce.py writes) -- whose import directory is the
    game's import list, and which carries no UID, so the loader's image check
    is off (0) for it."""
    d = open(image, 'rb').read()
    if image.lower().endswith(('.nax', '.lxe')):
        import lxce
        raw = d if d[:4] == b'LXCE' else lxce.unpack(d)
        return lxce.import_list(lxce.parse(raw)), 0
    return ([(n.split('[')[0].split('{')[0].lower(), o)
             for n, ords in e32imports.imports(d) for o in ords],
            e32imports.header(d)['uid3'])


def build(image):
    return build_imports(image_imports(image)[0])


def build_imports(imports):
    """The same pairing for any list of (library, old ordinal): an E32
    image's import section, or the import table of an engine the game loads
    itself and resolves through RLibrary::Lookup (Colin McRae's AirPlay
    engine, `lxce.py`)."""
    defs = find_defs()
    new = {lib: defs[lib] for lib in {l for l, _o in imports} if lib in defs}
    # A library with no .def in the release tree (estor, for One) is matched
    # against EKA2L1's epoc9.def list instead -- the same directory the
    # BY_ORDINAL and MANUAL targets fall back to below, so the two agree.
    fallback = epocdb.load(EPOC9)
    synth = {lib: fallback_table(fallback[lib]) for lib in {l for l, _o in imports}
             if lib not in defs and lib in fallback}
    rows = shimtable.match(imports, {'euser': KERNEL + '/kernel/eka/bmarm/7.0-euseru.def'},
                           new, epoc6=EPOC6, new_tables=synth)

    new_index = {lib: shimtable.index(symdef.load(path)) for lib, path in new.items()}

    # **The 9.x directory, extended past the source release.** A few libraries
    # a game imports have no .def in the release tree at all -- `msgs` is one
    # -- and BY_ORDINAL cannot name a target in a library there is no index
    # for. EKA2L1's own `epoc9.def` carries one list per library in ordinal
    # order: it agrees with the RM-409 ROM on the export count for `ezlib`
    # (86) and every euser address checked against the ROM's export directory
    # in E265 to E270 came out right. So it stands in where the release has
    # nothing, and never overrides the release where it has something.
    for lib in {l for l, _o in imports}:
        if lib in new_index or lib not in fallback:
            continue
        new_index[lib] = shimtable.index(fallback_table(fallback[lib]))

    helper_ords = {}
    for lib in ('dfpaeabi', 'drtaeabi', 'scppnwdl'):
        table = symdef.load('%s/%su.def' % (COMPSUPP, lib))
        helper_ords[lib] = {sym: o for o, (sym, _c) in table.items()}

    # (index, library, ordinal, signature, kind)
    out = []
    for i, lib, _o, sig, ordinal, _src in rows:
        # Checked before the ordinal, deliberately: these do have a 9.x
        # equivalent, and forwarding to it is exactly what must not happen.
        if sig and _is_framework_ctor(sig):
            out.append((i, None, LOCAL_NOOP, sig, KIND_LOCAL))
        elif lib in NGAGE_ONLY and (lib, _o) not in BY_ORDINAL:
            out.append((i, None, LOCAL_NOOP, sig, KIND_LOCAL))
        elif (lib, _o) in BY_ORDINAL:
            target, what, kind = BY_ORDINAL[(lib, _o)]
            if target == 'local':
                out.append((i, None, what, sig, KIND_LOCAL))
            else:
                o = new_index.get(target, {}).get(shimtable.norm(what))
                out.append((i, target, o, sig, kind if o else KIND_NONE))
        elif ordinal:
            out.append((i, lib, ordinal, sig, KIND_CALL))
        elif sig and 'Reserved' in sig:
            # Vtable padding. Symbian's _Reserved members have empty bodies and
            # exist only to hold a slot, so an empty body is the whole shim.
            out.append((i, None, LOCAL_NOOP, sig, KIND_LOCAL))
        elif sig in MANUAL:
            target, what, kind = MANUAL[sig]
            if target == 'local':
                out.append((i, None, what, sig, KIND_LOCAL))
            else:
                o = new_index.get(target, {}).get(shimtable.norm(what))
                out.append((i, target, o, sig, kind if o else KIND_NONE))
        elif sig in HELPERS:
            hlib, sym, kind = HELPERS[sig]
            o = helper_ords[hlib].get(sym)
            out.append((i, hlib, o, sig, kind if o else KIND_NONE))
        elif sig in LOCAL:
            out.append((i, None, LOCAL[sig], sig, KIND_LOCAL))
        else:
            out.append((i, None, 0, sig, KIND_NONE))

    out = [(i, lib, o, sig, KIND_SRET8 if kind == KIND_CALL and sig in RETURNS_STRUCT
            else KIND_DBL1 if kind == KIND_CALL and sig in PASSES_DOUBLE
            else kind) for i, lib, o, sig, kind in out]

    dlls = sorted({lib for _i, lib, _o, _s, kind in out if lib and kind != KIND_NONE})
    return out, dlls


def dynamic_map(lib):
    """Old ordinal -> 9.x ordinal for a library the game resolves at run time.

    Same pairing the imports get, run over every ordinal the old library has
    rather than only the ones the image imports, and 0 where 9.x dropped the
    function. euser's old side is the authoritative 7.0 def; the rest come from
    EKA2L1's EPOC6 database, as the imports do.
    """
    euser_def = KERNEL + '/kernel/eka/bmarm/7.0-euseru.def'
    if lib == 'euser':
        count = max(symdef.load(euser_def))
        old_sources = {'euser': euser_def}
    else:
        count = len(epocdb.load(EPOC6)[lib])
        old_sources = {}
    rows = shimtable.match([(lib, o) for o in range(1, count + 1)],
                           old_sources, {lib: find_defs()[lib]}, epoc6=EPOC6)
    return [r[4] or 0 for r in rows]


def emit(rows, dlls, path):
    lines = ['// Generated by gen_shim.py -- do not edit.',
             '//',
             '// One word per import: (DLL index + 1) << 16 | 9.x ordinal, or 0 when',
             '// nothing matched and the import keeps its reporting stub.',
             '',
             'extern "C" {',
             '']
    for i, dll in enumerate(dlls):
        name = dll if dll.endswith('.dll') else dll + '.dll'
        chars = ', '.join("'%s'" % c for c in name)
        lines.append('extern const unsigned short kShimDll%d[];' % i)
        lines.append('extern const unsigned short kShimDll%d[] = { %s };' % (i, chars))
    lines.append('')
    lines.append('extern const unsigned short *const kShimDllName[] = { %s };'
                 % ', '.join('kShimDll%d' % i for i in range(len(dlls))))
    lines.append('extern const unsigned char kShimDllLen[] = { %s };'
                 % ', '.join(str(len(d if d.endswith('.dll') else d + '.dll')) for d in dlls))
    lines.append('extern const unsigned int kShimDllCount = %d;' % len(dlls))
    lines.append('')

    index = {d: i for i, d in enumerate(dlls)}
    words = []
    for _i, lib, ordinal, _sig, kind in rows:
        if kind == KIND_LOCAL:
            words.append(KIND_LOCAL << 24 | ordinal)
        elif kind and lib:
            words.append(kind << 24 | index[lib] << 16 | ordinal)
        else:
            words.append(0)
    lines.append('extern const unsigned int kShimTable[] = {')
    for i in range(0, len(words), 8):
        lines.append('    ' + ' '.join('0x%08X,' % w for w in words[i:i + 8]))
    lines.append('};')
    lines.append('extern const unsigned int kShimCount = %d;' % len(words))
    # The two libraries the game loads by name and asks for ordinals.
    for lib in ('euser', 'efsrv'):
        table = dynamic_map(lib)
        name = lib.capitalize()
        lines.append('')
        lines.append('extern const unsigned short kShim%s[];' % name)
        lines.append('extern const unsigned short kShim%s[] = {' % name)
        for i in range(0, len(table), 16):
            lines.append('    ' + ' '.join('%d,' % w for w in table[i:i + 16]))
        lines.append('};')
        lines.append('extern const unsigned int kShim%sCount = %d;' % (name, len(table)))

    lines.append('')
    lines.append('}')
    open(path, 'w').write('\n'.join(lines) + '\n')
    return sum(1 for w in words if w)


if __name__ == '__main__':
    rows, dlls = build(sys.argv[1])
    n = emit(rows, dlls, sys.argv[2])
    imports, uid3 = image_imports(sys.argv[1])
    idx = hook_indices(imports)
    hpath = os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])), 'gate_imports.h')
    env = coeenv_methods(imports, epocdb.load(EPOC6),
                         {v for v in idx.values() if v != HOOK_ABSENT})
    hooked = {v for v in idx.values() if v != HOOK_ABSENT}
    ui = appui_methods(imports, epocdb.load(EPOC6), hooked)
    found = emit_hooks(idx, hpath, sys.argv[1], uid3, env, ui,
                       list_indices(imports, MILESTONES),
                       list_indices(imports, HOT),
                       divert_indices(imports))
    print('%s: %d CEikAppUi methods' % (hpath, len(ui)))
    print('%s: %d CCoeEnv methods' % (hpath, len(env)))
    print('%s: %d of %d hooks found' % (hpath, found, len(idx)))
    kinds = {}
    for _i, _l, _o, _s, k in rows:
        kinds[k] = kinds.get(k, 0) + 1
    print('%s: %d of %d imports answered across %d DLLs (%s)'
          % (sys.argv[2], n, len(rows), len(dlls), ', '.join(dlls)))
    print('   direct %d, remainder thunk %d, generated locally %d, unanswered %d'
          % (kinds.get(KIND_CALL, 0), kinds.get(KIND_REM, 0),
             kinds.get(KIND_LOCAL, 0), kinds.get(KIND_NONE, 0)))
