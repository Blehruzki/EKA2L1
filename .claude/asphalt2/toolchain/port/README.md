# Porting toolchain

Building Symbian binaries without a Symbian SDK.  Everything here writes a
format we have re-derived from shipped files, and every format is checked the
same way: **reproduce a stored value before writing one.**

| module | writes | how it is checked |
| --- | --- | --- |
| `mke32.py` | E32 executable images (V-format) | `uid_checksum` and the header CRC reproduce the values stored in the stock game exe exactly |
| `rscwrite.py` | the `.rsc` container and its unicode run encoding | used by both writers below |
| `mkreg.py` | `<app>_reg.rsc`, the app registration | `selftest` rebuilds the shipped `Asphalt2_full_reg.rsc` byte for byte |
| `mkloc.py` | `<app>.rsc`, the caption resource apparc reads next | `selftest` rebuilds the caption record of the shipped `asphalt2_full.rsc` byte for byte |
| `mksis.py` | a SIS package, from EKA2L1's own `EKA2L1HW` sample as the container template | `verify_pkg.py` re-hashes every install description against the shipped data |
| `relocs.py` | the code relocation section | `selftest` rebuilds a shipped image's section byte for byte |
| `importsec.py` | the EABI import section | `selftest` rebuilds a shipped image's 26-DLL section byte for byte |

## Gate 1 — done

`build_gate1.py` goes clang -> flat ARM -> E32 -> registration resources -> SIS
with no SDK anywhere in the chain.  The app deliberately reads `0xDEAD0000`.

Installed from `gate1.sis` and launched by UID, EKA2L1 reports:

```
Service.Applist: Found app: Gate1, uid: 0xE0001001
Kernel: Spawned process: gate1, entry point = 0x0
Kernel: gate1.exe (UID3=0xE0001001) runtime code: 0x70000000
Kernel: Access violation reading address 0xDEAD0000 in thread Main
Kernel: Last instruction: ldr r0, [pc, #4] (0xe59f0004)
CPU: pc: 0x70000000   r0: 0xdead0000
```

That is our first instruction, our literal pool and our planted address, at the
base of our own code section.  The image format, the loader's acceptance, the
app registration and the package all hold.

### Things worth knowing, learned here

- `header.entry_point` is an **offset** from the code base, not an address.
- The entry point runs in **ARM** mode, is entered with `r1` = stack top and
  `r4` = 0, and `lr` = 0 -- so returning from it is not a way to exit.
- `text_size == code_size` keeps the loader's import-address-table walk empty.
- For EABI images the import block holds **offsets into the code** at which a
  word `ordinal | (adjustment << 16)` sits, not an import table to fix up.
- apparc drops an app entirely if its localisable resource file is missing, so
  a registration resource alone is not enough to get listed.

## Gate 2 — 421 of 462 named

`resolve_imports.py` names an image's imports from Symbian `.def` files.
Run against the N-Gage build:

```
resolve_imports.py 6RBC.APP euser.dll=<src>/kernel/eka/bmarm/7.0-euseru.def \
                   --epoc6 src/emu/bridge/include/bridge/epoc6.def
```

| source | covers | standing |
| --- | --- | --- |
| `kernel/eka/bmarm/7.0-euseru.def`, from the Symbian Foundation source release | EUSER, 164 imports | authoritative: the Symbian 7.0 ARM build, the N-Gage's own ABI |
| EKA2L1's bundled `epoc6.def` | 16 more libraries, 257 imports | ~95% confidence, measured -- see below |
| nothing | GAMECOMMS 20, GAMEUTILS 6, ARENAFRAMEWORK 4, NOKIAFC 1 | Nokia N-Gage libraries, never open-sourced; these 41 need reverse engineering |

The result is in `../ngage-imports.txt`.

### Two things that had to be checked, not assumed

**Ordinals are not stable across Symbian versions.** Of the 1680 EUSER ordinals
present in both the 7.0 and the 9.x ARM def files, **8 agree** -- 0.5%. The
files are ordered alphabetically by mangled name, so a single added export
shifts everything after it. Naming a 7.0s binary's imports from 9.x def files
would produce a confident, complete and almost entirely wrong answer.

**EKA2L1's `epoc6.def` is ordinal-ordered.** It lists 556 libraries' exports in
file order with no ordinals written down, so that had to be established rather
than assumed: against the authoritative 7.0 file, 1574 of euser's 1646
positions agree (95.6%), and 155 of the 164 ordinals this game imports. Most
disagreements are two names for one function (`memclr` / `Mem::FillZ`,
`User::Allocator` / `User::Heap`). It is a strong lead per line, not proof --
confirm at the call site before relying on one.

`gnuv2.py` demangles the GCC 2.x names, which modern binutils no longer does.
It returns the original string whenever it is unsure, so a name is never
quietly turned into a wrong one.

### What it says about the port

The EIKCORE, CONE and AVKON surface is dominated by framework base-class
methods -- `CEikAppUi`, `CEikApplication`, `CEikDocument`, `CCoeControl`,
`CCoeAppUi`, `CAknAppUi` -- including the `_Reserved` vtable padding slots.
That is the shape of an app built on the S60 application framework, and it is
exactly what gate 3 has to reproduce: a shim has to present those classes with
their GCC98r2 vtable layouts, not merely provide the functions.

## Gate 3 — the ABI holds

`vtables.py` reconstructs the image's vtables slot by slot. It works because
imports in an EKA1 image go through a 16-byte veneer whose literal is
relocated, so a vtable slot filled by an inherited method names the base-class
ordinal that fills it. A derived class's vtable therefore states the layout of
every framework class the game builds on; the result is in
`../ngage-vtables.txt` (47 vtables).

Measured from the binary, not assumed:

| | |
| --- | --- |
| vptr | at object offset 0, pointing **8 bytes before slot 0** — 160 stored vptrs use `table-8`, **none** uses `table+0` |
| those 8 bytes | two zero words: offset-to-top, and typeinfo, null with RTTI off (41 of 47 tables confirm directly; the other 6 abut another table) |
| slot 0 | the destructor |
| entries | plain function pointers, no per-entry delta — so `-fvtable-thunks` |
| a virtual call | `ldr r3,[obj]` / `ldr r3,[r3,#8+4*slot]` / `mov lr,pc` / `bx r3`, smallest displacement seen being exactly 8 |
| mixins | a second vptr at object offset +4, as every C-class-plus-M-mixin has |

EABI points its vptr straight at slot 0, with offset-to-top and typeinfo at -8
and -4. **The two layouts differ by exactly that 8-byte bias** — which is much
less than feared.

`build_gate3.py` proves it runs. A clang-built C++ class has its EABI vtable
republished in the old shape and is then called the old way. Seven checks,
reported as the reason code of a panic -- a phone shows `<thread> <category>
<reason>` on screen and never shows a fault address, so all seven passing reads
**`Main GATE3 127`**:

| bit | check |
| --- | --- |
| 0,1,2 | old-style dispatch through slots 0, 1 and 2 reaches `a`, `b` and `c` and reads `this->magic` correctly — bit 1 uses the exact four-instruction sequence above |
| 3 | biasing the vptr the EABI way reaches the **wrong** function, so the three above are not accidentally right |
| 4 | the class still works when called normally from C++ |
| 5 | a class with a virtual destructor dispatches correctly once the two Itanium destructor entries (complete, then deleting) are collapsed into the single one GCC 2.x emits |
| 6 | a mixin: old code holding a pointer to the second base dispatches through the vptr at +4, and `this` is adjusted back to the whole object before the method sees it |

Result: **all seven**, in the emulator and on the phone.

Bit 5 is the one that costs work in a shim. Every Symbian framework class has a
virtual destructor, so every slot map needs that translation; slot numbers are
not simply shifted.

This also added **relocations** to `mke32.py`, which any real binary needs.
`relocs.py` finds them by linking twice a page apart and comparing: a word that
is identical is position-independent, one that differs by exactly the base
delta is an absolute address, and anything else raises instead of producing a
wrong table.

### Still unproven

Argument passing. APCS and AAPCS disagree about 64-bit arguments — AAPCS wants
them in an even register pair — and about struct return. Nothing here exercises
either, and Symbian passes `TTimeIntervalMicroSeconds` and friends by value.

## Gate 4 — the N-Gage binary runs

`gate4.cpp` is a loader. It reads `6RBC.APP`, copies its code into a chunk made
with `RChunk::CreateLocalCode` (the mechanism JITs use, so the memory is
executable), rebases it with its own relocation table, points all 462 imports
at stubs, and enters it -- entry point first with `EDllProcessAttach`, then
export ordinal 1, which is `NewApplication`.

Each stub is 16 bytes built at run time, carrying its own index:

```
ldr r0, [pc, #0]    @ the import index
ldr pc, [pc, #0]    @ the reporter
.word index
.word gate4_report
```

Result in the emulator:

```
Thread Main panicked with category: G4IMP and exit code: 265
```

Import 265 is `euser` ordinal 3, **`User::AllocZL(int)`** — the game allocating
its application object, which is exactly what `NewApplication` does first.

So: an EKA1 N-Gage binary, loaded on an S60v3 system, 9165 words relocated, 462
imports bound, its own instructions executing and calling out through our table.
The panic categories say how far it got — `G4FS` file, `G4MEM` memory, `G4HDR`
a header check, `G4IMP` an import reached, `G4RET` returned without calling.

### The forwards

`gen_shim.py` turns `shimtable.py`'s pairing into data gate 4 links against: a
list of 9.x DLLs and one word per import saying which DLL and ordinal answers
it. 351 of 462 are forwarded across 14 libraries.

They are resolved at run time with `RLibrary::Lookup`, not through our own
import section, for two reasons. An import section is checked when the image
loads, so a single ordinal a device does not have would stop the program
starting with no way to say which one; a lookup returning null can be counted.
And the ordinals come from the Symbian source, which is a later 9.x than any
one phone, so some will be wrong.

### The compiler helpers

GCC98r2 kept its compiler helpers in euser; EABI keeps them in the runtime
libraries the SDK links against, under their AEABI names. euser 9.x exports
none of them, so they are not a renumbering but a move. The routines are the
same and so are the registers -- a double arrives in r0:r1 and r2:r3 either way
-- so most are a straight forward to `dfpaeabi` or `drtaeabi`, and operator new
and delete to `scppnwdl`. That is 20 more imports answered.

Four needed more than a name change, and gate 4 generates those into the chunk
alongside the reporting stubs:

| import | why |
| --- | --- |
| `__modsi3`, `__umodsi3` | `__aeabi_idivmod` returns the quotient in r0 and the remainder in r1; these must return the remainder, so a thunk moves it across |
| `__negsf2` | no AEABI equivalent: `eor r0, r0, #0x80000000` |
| `__pure_virtual` | a call through a vtable slot the binary never filled; panics as `G4PUR` |

`__divsi3` and `__udivsi3` are forwarded to **divmod** rather than to
`__aeabi_idiv` and `__aeabi_uidiv`. Those two sit at the end of the def and are
absent from shipped `drtaeabi` builds, while divmod returns the quotient in r0,
which is exactly what they want.

With the forwards in place the game gets much further:

```
Thread Main panicked with category: G4RET and exit code: 389001
```

389 forwards resolved, 1 missing, and **`NewApplication` returned a live
object** — the same numbers on the emulator's 9.4 ROM and on a real 9.2 N95.
The one that will not resolve is import 146, `CEikApplication::OpenAppInfoFileLC`,
which 9.x removed along with AIF files when registration moved to the resource
format `mkreg.py` writes. It is gone rather than renumbered, so it needs a
hand-written shim if the game ever calls it — the N-Gage binary ran its entry point, its static constructors and
its application factory, allocating and constructing through S60v3's own euser,
cone, eikcore and avkon.

Forwarding is not correct in general, and this is a spike rather than a port.
A 9.x framework class is not the same size as its 7.0s ancestor, so old code
that allocates `sizeof` the old class and calls a forwarded 9.x constructor
will corrupt the heap. It works here because the path taken is dominated by
leaf functions -- allocation, descriptors, arithmetic -- whose layouts did not
move. The classes the game derives from will each need real work.

### Closing the near-misses

390 of 462 are answered now. Three things were being missed for reasons that
were fixable rather than fundamental:

- **Templates.** GCC 2.x mangles `TBuf<256>` as `t4TBuf1i256`, which neither
  end of the matcher understood. Both directions handle it now, and the four
  affected names mangle back byte-exactly.
- **`_Reserved` members.** Symbian's vtable padding, whose bodies are empty and
  which 9.x stopped exporting. An empty body is the whole shim, so these get a
  generated `mov r0,#0 / bx lr`.
- **Six that moved rather than vanished**, each checked against the 9.x def
  rather than assumed: `RThread::SetExceptionHandler` became
  `User::SetExceptionHandler`; `RFsBase::Close` became `RHandleBase::Close`;
  `User::ReAllocL` gained a mode argument, so it gets a thunk that passes zero;
  `CBase`'s constructor and destructor are empty and no longer exported; and
  only the 16-bit `Mem::Compare` survives, so the 8-bit one is written here.

What remains unanswered is genuine work, not missing information: `CServer`,
`CSession`, `TTrap` and `TInt64` really are gone from 9.x, 31 imports belong to
Nokia's N-Gage-only libraries, and ten sit past the end of what EKA2L1's EPOC6
database lists for their library, so nothing here can name them.

### What NewApplication actually builds, and why forwarding stops here

Disassembling export ordinal 1 says exactly what the game hands back:

```
mov r0, #556               @ sizeof its CApaApplication subclass
bl  User::AllocZL          @ import 265
bl  CEikApplication::CEikApplication()   @ import 169, forwarded to 9.x eikcore
ldr r3, [pc, #8]           @ the vtable at code+0x181084, minus the 8-byte bias
str r3, [r4]
```

That object's vtable has 14 slots, four of them the game's own, and it is the
specification a wrapper has to meet:

```
[ 0] the game's destructor        [ 7] Capability
[ 1] PreDocConstructL             [ 8] Reserved_1
[ 2] CreateDocumentL              [ 9] GetDefaultDocumentFileName
[ 3] AppDllUid  (the game's)      [10] BitmapStoreName
[ 4] OpenIniFileLC                [11] ResourceFileName
[ 5] OpenAppInfoFileLC            [12] the game's
[ 6] AppFullName                  [13] the game's
```

Slot 5 is the one import that cannot resolve, and it is inherited rather than
overridden -- so it only matters if the 9.x framework calls it.

**This is where forwarding stops being correct.** The third line above is a 9.x
`CEikApplication` constructor running with `this` pointing at an object laid
out by the 7.0s compiler. It does not crash, because the block is zeroed and a
constructor writes only a couple of words, but nothing guarantees 9.x puts
`iCoeEnv` and `iResourceFileOffset` where the old code expects them. Forwarding
is sound for leaf functions -- allocation, descriptors, arithmetic, file I/O --
and unsound for the framework classes the game *derives* from, which is
roughly the cone, eikcore, eikdlg and avkon share of the table.

The way out is not more forwarding. Those base classes have to be reimplemented
against the old layout, and the old object handed to the 9.x framework only
through a wrapper that owns a 9.x-layout object of its own. That also needs the
program to be a real S60v3 GUI application, because none of the lifecycle past
this point runs without a `CEikonEnv`: standalone, there is nothing to call
`PreDocConstructL` on behalf of.

### The startup nobody does for you

This is where the first attempt died, with `pc` at zero and `lr` inside euser:
`User::Alloc` walked a null allocator. An exe built by the SDK links
`eexe.lib`, whose `_E32Startup` does the work; ours links nothing. The Symbian
source spells the sequence out in `kernel/eka/euser/epoc/arm/uc_exe.cpp`:

```
UserHeap::SetupThreadHeap(aNotFirst, cinfo);   // euser 1360
User::InitProcess();                           // euser 585, statics for linked DLLs
E32Main();
```

The kernel leaves `SStdEpocThreadCreateInfo` at the initial stack pointer and
passes the startup reason in r4, which is what a real entry point forwards. Our
`_start` now does the same. Without it a process has no heap at all.

## Step 1 — a real GUI application

Nothing past `NewApplication` runs without a `CEikonEnv`, so the program has to
stop being a bare exe and become an application the UI framework starts.

A GUI app's `E32Main` hands a factory to `EikStart::RunApplication` (eikcore
ordinal 394), which brings the framework up and then asks the factory for the
application object. `TApaApplicationFactory` is four words -- a type tag, the
payload, a cached pointer and a spare -- and is trivially copyable, so AAPCS
passes it in r0..r3 rather than by reference, with type 0 meaning the payload is
a function pointer. That register layout is the one thing here no def file can
confirm, which is why `build_gate5.py` checks it on its own before anything is
built on top:

```
Thread gate5 panicked with category: G5NEW and exit code: 1
```

The framework started and called our factory. The thread is named `gate5`
rather than `Main`, which is the framework having taken over process startup.

### The application object

Declaring the base classes and hoping the layout matches is the fragile way to
do this. avkon and eikcore **export their vtables** (`_ZTV15CAknApplication` is
avkon ordinal 3652), so the sturdy way is to look the real one up, copy it, and
patch in the slots a concrete application has to supply. The object itself is
built exactly as the game builds its own: zeroed memory, the exported base
constructor (`CEikApplication::CEikApplication()`, eikcore 64), then the vptr.

`CAknApplication::CAknApplication()` is not exported, which is presumably why
the game calls the CEikApplication one too.

The slot numbers come from the declaration order in `apaapp.h`, `EIKAPP.H` and
`aknApp.h` -- base virtuals first, a destructor taking two Itanium slots where
its base declared it:

```
 0,1 ~CBase           6  OpenIniFileLC        12 GetDefaultDocumentFileName
 2   Extension_       7  AppFullName          13 BitmapStoreName
 3   PreDocConstructL 8  Capability           14 ResourceFileName
 4   CreateDocumentL(CApaProcess*)            15,16 CEikApplication_Reserved1,2
 5   AppDllUid        9  NewAppServerL        17 CreateDocumentL()
                      10,11 CApaApplication_Reserved1,2
```

Patching slots 5 and 17 and handing the object back gets:

```
Thread gate5 panicked with category: G5DOC and exit code: 1
```

The framework took the object, called `AppDllUid` on it, and asked for a
document -- so all three slot numbers are right.

### The resource file needed a NAME

With the real `PreDocConstructL` the framework first panicked `CONE 15`,
`ECoePanicResourceFileHasNullName`. Reading `COEMAIN.CPP` rather than guessing:
the panic is `RResourceFile::Offset()` returning zero. A `.rss` file's NAME
statement sets an offset carried in the top twenty bits of the signature
resource's second word, and every resource id in the file is that offset plus
an index. The shipped game file has `0x083FA001` there; `mkloc.py` was writing
`1`.

Nothing checks that the offset matches any particular name, so any non-zero
value works as long as the ids handed out agree with it -- which is why
`CAPTION_RES_ID` now carries it. Both resource selftests still reproduce their
shipped files byte for byte.

Then `USER 42`, `ETHeapBadCellAddress`: the object was too small. The headers
in the Symbian source release are a later 9.x than any one phone, so a `sizeof`
computed from them would not be the device's -- and over-allocating a zeroed
block cannot corrupt anything. 2048 bytes it is.

With both fixed and nothing stubbed, the framework loads the resource file,
fetches the caption and capability from apparc, and asks for a document:

```
Thread gate5 panicked with category: G5DOC and exit code: 1
```

### The document, and why counting headers stopped working

`CAknDocument::CAknDocument(CEikApplication&)` is avkon ordinal 131 and its
vtable is 3632, so the object goes together like the application's -- and the
constructor works: with it called, the framework goes on to use the document.

The slot numbers did not survive the same treatment. Counting virtual
declarations in `apadoc.h` and `EIKDOC.H` put `CreateAppUiL` at 19. Counting is
a guess, because an override in a derived class is virtual whether or not the
header repeats the keyword, so the count cannot tell an override from a new
slot. Measuring instead: give the document a vtable whose every slot is a
16-byte trampoline carrying its own index -- the same trick gate 4 uses on
imports -- and let the framework name what it calls.

```
Thread gate5 panicked with category: G5DOC and exit code: 21
```

Two off. Worth knowing before building on it.

Two other things that looked like the answer and were not:

- Finding the pure slots by looking for a **repeated** value fails when there
  is exactly one, because then nothing repeats.
- Comparing against `__cxa_pure_virtual` from `drtaeabi` fails too: each DLL
  links its own copy, so the addresses do not match across modules.

### Chaining wrappers, and what they found

Being the first virtual called is not the same as being `CreateAppUiL`, and the
trampoline run conflated the two. A wrapper that records the call and then
chains to the real implementation keeps the framework working while the order
is observed. Each slot gets 64 bytes:

```
push {r0-r3, r12, lr}     @ the arguments, untouched, and the return address
ldr  r0, [pc, #24]        @ this slot's index
ldr  r1, [pc, #24]        @ the shared counter
ldr  r2, [pc, #24]        @ the recorder
mov  lr, pc
bx   r2
pop  {r0-r3, r12, lr}     @ put the arguments back
ldr  pc, [pc, #12]        @ and fall into the real implementation
```

Restoring sp before the tail jump matters: a function returning a large object
by value, or reading arguments past r3, would otherwise find the stack moved
under it.

The order is slot 21, then slot 19. Chaining 19 to its real implementation
raises an unhandled exception -- `USER-EXEC 3` -- which is what calling a pure
virtual does. So **19 is `CreateAppUiL`**, which is what counting the headers
had said all along; 21 is something else that the real vtable handles for us.

With 19 patched and the rest chained, and the app UI built from
`CEikAppUi::CEikAppUi()` with `CEikAppUi`'s own vtable -- constructor and
vtable from the same class, since `CAknAppUi`'s constructor is not exported and
mixing the two would describe an object that was never laid out that way:

```
Thread Gate5 panicked with category: CONE and exit code: 14
```

`ECoePanicNoResourceFileForId`. The application, the document and the app UI
are all built and `ConstructL` is running; it is asking for a resource by id
and the file this toolchain writes does not contain it.

### The app UI, and why the resource file was the wrong task

`CONE 14` looked like a call for an application resource file. It is not.
`CEikAppUi::BaseConstructL` (eikcore 150) takes flags, and `ENoAppResourceFile`
makes it skip the application info resource and build resource-independent
screen furniture instead. An application without its own resource file is a
supported case, so saying so is better than writing a file to satisfy a read
that does not have to happen.

The wrappers found the app UI's `ConstructL` at slot 16, the same way they
found the document's `CreateAppUiL` at 19 -- with one correction worth keeping:
a recorder that panics never reaches the slot it was meant to identify, since
the wrapper records *before* it chains. Once a slot is known its recorder has
to become a no-op.

### Finding a slot exactly, rather than by when it is called

`BaseConstructL(ENoAppResourceFile)` faulted reading address 8, and the two
obvious explanations were both wrong. `iEikonEnv` is a macro for `iCoeEnv`
(EIKDEF.H), so a null there would explain it -- but scanning the object for the
value `CCoeEnv::Static()` returns finds it at offset 4, set. And slot 16 might
have been misidentified, as the document's first-called slot was -- but
`CEikAppUi::ConstructL` is **exported**, eikcore 140, so looking that address
up and finding it in the vtable settles it: slot 16.

Looking up an exported function and finding its address in the table is worth
preferring over watching which slot gets called first. It is exact, it needs no
run, and it does not confuse "the first virtual the framework happens to call"
with "the virtual we mean". It only works where the function is exported --
`CreateAppUiL` is pure, so the document still needed the wrappers.

With both confirmed, the fault was really inside `BaseConstructL`, in
`CreateResourceIndependentFurnitureL`: a status pane needs more of an
environment than an app this bare has. `ENoScreenFurniture` alongside
`ENoAppResourceFile` is the documented way to say so, and then it returns.

### Take the vtable from the object, not from an ordinal

Application, document and app UI are all built and the framework runs
`ConstructL` to completion in the emulator; an N95 gave `KERN-EXEC 3` on the
same package. The emulator device here is RM-409, a Nokia 5320 on `epoc93fp2`
-- S60v3 FP2 -- against an N95's FP1, so both are S60v3 and the gap is 9.3
against 9.2.

Looking a vtable up by ordinal is the fragile part of that gap. Ordinals for
these libraries come from the Symbian source release, later than either phone,
and a vtable fetched by a wrong ordinal is a real symbol that is not a vtable
-- an access violation with nothing to say about which lookup was wrong.

A constructor has already put the right vtable in the object, so read it back
from there:

```cpp
static const u32 *vtable_of(const u32 *object)
{
    return (const u32 *)object[0] - VT_HEADER;
}
```

No ordinal, and a constructor ordinal cannot fail the same way: it lives in the
import section, which the loader resolves when the image starts, so a wrong one
stops the app rather than corrupting it. It also keeps constructor and vtable
in step by construction, which pairing them by hand had not -- the application
was being built with `CEikApplication`'s constructor and `CAknApplication`'s
vtable, two classes whose layouts need not agree.

Letting `ConstructL` return rather than stopping there faults shortly
afterwards, which is fair: an app UI with no control and no view has nothing
for the framework to run. Giving it one belongs with step 2, where the old
binary's own classes start supplying the content.

## Step 2 — stop forwarding what must not be forwarded

Forwarding a leaf function is sound: allocation, descriptors, arithmetic and
file I/O have the same layout on either side. Forwarding a base-class
constructor is not, and gate 4 was doing it. The game allocates its application
object at 556 bytes -- the size the 7.0s compiler computed -- and then calls
the base constructor; sending that to the 9.x one runs 9.x code writing 9.x
field offsets into an object that was never laid out that way. Nothing makes
`iCoeEnv` and `iResourceFileOffset` sit where the old code expects them.

`gen_shim.py` now intercepts the constructors and destructors of the classes
the game derives from -- `CCoeControl`, `CCoeAppUi`, `CEikApplication`,
`CEikDocument`, `CEikAppUi`, `CEikDialog`, `CEikBorderedControl`, and the Akn
classes -- **before** looking for a 9.x ordinal, because they do have one and
using it is the thing to avoid. Six of the 462 move from forwarded to an empty
body, and the game still hands back a live application object:

```
Thread Main panicked with category: G4RET and exit code: 389001
```

An empty body on zeroed memory is an approximation, not a reimplementation: it
leaves the old fields at zero rather than filling them with values meant for a
different object, which is closer to right but not right. Implementing them
against the old layout is the rest of step 2, and the layouts are readable from
the game's own code -- the allocation sizes and the offsets its methods touch.

## Step 3 — the framework asks, the N-Gage binary answers

Gate 4 loaded the old binary and ran its code; gate 5 made this a real S60v3
GUI application. Gate 6 joins them.

The framework wants a `CApaApplication`. The game's own `NewApplication` builds
one, but it is an old-ABI object the 9.x framework cannot call. So the
framework gets a 9.x-layout object of ours, built the ordinary way -- zeroed
memory, `CEikApplication::CEikApplication()`, the vtable read back out of the
object -- with one slot pointed at the old object instead:

```cpp
extern "C" u32 gate6_app_dll_uid(void *self)
{
    return (u32)old_call((void *)((u32 *)self)[WRAP_GAME_APP], OLD_APP_DLL_UID);
}
```

`old_call` is the GCC98r2 virtual call gate 3 measured in the game's own code:
the vptr at object offset 0, pointing eight bytes before slot 0. The wrapper
carries the old object in a word past anything `CEikApplication` uses, since a
writable global would need a `.bss` section these images do not have.

The two vtables are not the same shape -- 9.x splits the destructor across two
slots and has no `OpenAppInfoFileLC` -- so `AppDllUid` is slot 5 on one side
and slot 3 on the other. A translation, not an offset.

Called the way the framework calls it:

```
Thread gate6 panicked with category: G6UID and exit code: 270521389
```

`0x101FD42D`, the game's own UID3. The whole chain ran: our app started, the
N-Gage binary was loaded and relocated, its 462 imports bound, its
`NewApplication` built its application object, and a 9.x virtual call reached
it through its own calling convention and came back with its answer.

### Only the slots the game overrides

The obvious next move -- bridge every slot whose old counterpart is known --
faults immediately. Bridging an *inherited* slot sends the framework through
the game's veneers into our shim and back into 9.x code, running with `this`
pointing at the old 556-byte object. The 9.x implementation reads fields that
are not where it expects them and dies.

So the wrapper forwards only what the game actually overrides, and leaves every
inherited slot with its 9.x implementation, which then runs against a 9.x
object as it was built to. The game's vtables say what that is:

```
application  0 destructor   3 AppDllUid   12 CreateDocumentL()   13 ?
document     0 destructor   9 ?           17 CreateAppUiL()
```

Old application slot 12 is a one-instruction branch to a function that
allocates 0x24 bytes and constructs a document with the application as its
argument -- `CreateDocumentL()`, the no-argument factory, which 9.x puts at
slot 17. Old document slot 17 allocates 0x68 bytes: `CreateAppUiL()`, 9.x slot
19. Both were read from the binary, not guessed:

```
00002368  eaffffab  b #0x221c        @ CreateDocumentL()
0000221c  e92d4030  push {r4, r5, lr}
00002224  e3a00024  mov r0, #0x24
00002228  eb045ae2  bl #0x118db8     @ operator new
```

Destructors stay 9.x -- the wrapper is a real 9.x object and has to be torn
down as one -- so the old objects leak, which for now costs nothing.

`AppDllUid` turned out to be the one slot that must *not* be bridged. With the
game answering it, the framework looked the application up by the N-Gage UID,
found no registration for it, and left with `KErrNotFound` right after opening
the resource file. The UID is the framework's name for us, not for the game, so
it stays ours -- the same one the registration resource carries.

What is left is two parallel object graphs: the game keeps the objects its own
code built, and each 9.x wrapper holds a pointer to its counterpart in a word
past anything the 9.x class uses. `CreateDocumentL` calls the old slot, takes
the old document, and hands back a `CAknDocument` of ours that remembers it;
`CreateAppUiL` does the same one level down.

```
Thread Gate6 panicked with category: G6CHN and exit code: 3
```

Application, document and app UI -- the game's code built all three, on S60v3,
and the framework accepted a wrapper for each.

### The other direction

Everything so far was the framework calling the game. `ConstructL` is the game
calling the framework, on itself -- and the old object is exactly what a 9.x
implementation must not be given. Its slot is 13; slot 17 compares its argument
against 0x100, so that one is `HandleCommandL`. What it does:

```
BaseConstructL(0)                      avkon, import 9
TTrap::Trap(err) { ... } UnTrap()      euser, imports 372 and 373
[r5+0x60] = new <control>
ApplicationRect()                      eikcore, import 172
<control>->ConstructL(rect)
AddToStackL(control, 0, 0)             cone, import 51
SetKeyBlockMode(1)                     avkon, import 39
[r5+0x64] = KeySounds()                avkon, import 25
PushContextL(0x08cc0116)               avkon, import 35
```

So the shim needs to divert a call rather than forward it. A resolved import's
stub slot is spare -- its address went straight into the table -- so the
diversion is built there, carrying a context word in r2 while r0 and r1 pass
through:

```
ldr r2, [pc, #4]
ldr pc, [pc, #4]
.word context
.word our function
```

The context holds the 9.x wrapper, and `BaseConstructL` becomes ours: it drops
the old object, and calls `CEikAppUi::BaseConstructL` on the wrapper with the
flags gate 5 established rather than the game's zero, since nothing here has
screen furniture to construct yet.

EKA1's trap harness has no 9.x counterpart at all -- `TRAP` became a thread
trap handler -- so `TTrap::Trap` is generated locally as "first pass, no error"
and `UnTrap` as nothing. A leave inside the body then propagates to the
framework's own TRAP instead of being caught. Only code that actually leaves
can tell the difference.

With those three in place the game's `ConstructL` runs past `BaseConstructL`,
past the trap, and stops at:

```
Thread Gate6 panicked with category: G6IMP and exit code: 464459
```

Import 459: `NOKIAFC` ordinal 1. That is the N-Gage frontier -- 31 imports
across GAMECOMMS, GAMEUTILS, ARENAFRAMEWORK and NOKIAFC exist only in the
N-Gage ROM, and each has to be read out of it.

Read out of the game rather than out of the ROM: the trapped body builds a
`TBuf<256>`, switches on the language for one of five strings -- "Invalid game
card", "Carte de jeu non valable", "Tarjeta de juego inválida", "Ungültige
Spielkarte", "Scheda gioco non valida" -- and passes it, with the ASCII name
"N-Gage", to that one export. Nothing on an S60v3 phone would ever show that
message, so the stand-in succeeds and does nothing. Stand-ins for the N-Gage
libraries live in a table keyed by library and ordinal, since there is no
signature to match on.

Then `ApplicationRect`, `AddToStackL` and `SetKeyBlockMode`, all called on the
old object, all wanting the 9.x implementation with the wrapper instead. Those
need no code of their own, just the argument swapped, so the thunk does it:

```
ldr rN, [pc, #8]    @ rN = the cell holding the wrapper
ldr rN, [rN]
ldr pc, [pc, #4]
```

`ApplicationRect` returns a `TRect`, so r0 is the return buffer and `this` is
in r1 -- which is why the thunk takes the register rather than assuming r0.

### The frontier: the control

Past those, `ConstructL` builds a control and calls
`CCoeControl::CreateWindowL()` on it. That control is the game's object too, and
it is the first thing here there can be more than one of: a single wrapper
pointer in the context is not enough, it needs an old-object-to-wrapper map, and
the thunks that consult it will have to do the lookup themselves, since a call
with four arguments has no spare register to carry the context in.

```
Thread Gate6 panicked with category: G6CHN and exit code: 5
```

That is `CreateWindowL` diverted to a stop, rather than letting cone walk an
object it cannot read.

## Not done yet

- **The shim itself.** 462 stubs currently all panic. Each has to become a real
  implementation, or a forward to the S60v3 equivalent.
- **Calling convention.** APCS and AAPCS disagree about 64-bit arguments and
  struct return, and nothing has exercised either. Gate 4 is where that will
  show up, since the game's own code is now calling our functions.
- **Reading stock images.** E32 code is compressed with Symbian's own deflate
  (`0x101F7AFC`), which is not zlib; reading the import section of a shipped
  binary needs that inflater ported.

## Reading the ROM

The question the wrapper ran into -- what the old class layouts actually are --
is answered by the N-Gage ROM itself, which holds the 7.0s framework the game
was built against. EKA2L1 extracts a ROM into its z drive, and those files are
not E32 images: no `EPOC` signature, no import section, no relocations. Code in
a ROM is executed in place, so it is linked for its final address and every
pointer in it is already right.

What there is, per `loader/romimage.h`: a `TRomImageHeader` -- 100 bytes up to
EKA2, 120 after -- then the code, with an export directory inside it holding one
absolute address per ordinal. `romimg.py` reads both eras; which header length
applies is settled by trying each and keeping the one whose export directory
reads back as addresses inside the code, rather than by trusting the device.
Two things the format lets us check, and it does: the DLL reference table
follows the code exactly, and every export points inside it. A few of the
extracted files stop short of their own code, so anything past the end of the
file is reported missing rather than read as zeroes.

### What it says about the old layouts

`CCoeControl::CCoeControl()` -- cone ordinal 236, Thumb, since the N-Gage ROM
is Thumb-compiled:

```
push {r4, lr}
bl   CBase::CBase()
[this+4]  = <vtable>        @ a second base, at offset 4
[this+0]  = <vtable>
[this+0x10] = 0
[this+0x14] = 0
[this+0x18] = 0
[this+0x1c] = 0
r0 = this + 0x28; bl ...    @ a subobject at 0x28
bl   CCoeEnv::Static()
[this+8] = r0               @ iCoeEnv
```

So old `CCoeControl` is not four bytes after all. It has a base of its own at
offset 4 -- which is why every class derived from it writes a vtable there too,
and why the offsets looked impossible -- and `iCoeEnv` at offset 8. That is
exactly the `[control+8]` the game reads.

`CCoeControl::SystemGc()` is two instructions, and settles the rest:

```
ldr r0, [r0, #8]        @ iCoeEnv
ldr r0, [r0, #0x34]     @ iSystemGc
```

`CCoeEnv::SwapSystemGc` agrees: `iSystemGc` is at 0x34. With the member order
from `COEMAIN.H` -- `iAppUi, iFsSession, iWsSession, iRootWin, iSystemGc,
iNormalFont, iScreen` -- that puts `iScreen` at 0x3c and `iWsSession` at 0x20,
which is precisely what the game reads out of its `iCoeEnv`: the address of
`+0x20` and the pointer at `+0x3c`, handed to its graphics object along with
`Window()`. A window server session, a screen device and a window.

The same measurement on the 5320's cone puts 9.x `iSystemGc` at 0x40, so the
class grew by 12 bytes ahead of it. Nothing between `iSystemGc` and `iScreen`
changed size, so 9.x `iScreen` is at 0x48; where `iWsSession` landed is not
settled by this and still has to be measured.

Neither `WsSession()` nor `ScreenDevice()` is exported, in either era -- both
are inline, which is why the game reads the fields directly and why the port
has to as well.

## The control, and the environment behind it

The game constructs its own control, so unlike the application, the document
and the app UI, its base class cannot merely be wrapped -- it has to be right.
`CCoeControl::CCoeControl()` is therefore generated rather than forwarded: it
writes the old layout, zeroes to 0x30 and puts `iCoeEnv` at offset 8, and
returns the object the way a GCC98r2 constructor does.

What goes in `iCoeEnv` is the neat part. The whole tail of `CCoeEnv` moved
twelve bytes between the two eras and nothing in it changed size -- measured in
both ROMs at four fields:

```
              7.0s    9.x
iAppUi        0x18    0x24
iWsSession    0x20    0x2c
iRootWin      0x2c    0x38
iSystemGc     0x34    0x40
iScreen       0x3c    0x48
```

So the game does not need a synthetic environment at all. Hand it the real one
as a pointer twelve bytes past itself and every offset it reads lands on the
right 9.x field: the real window server session, the real screen device, by
reference, through its own layout. `CCoeEnv::Static()` returns the shifted view
too, since the game calls it directly as well -- `Static()` then `[env+0x18]`
is how it finds the app UI.

A window belongs to a 9.x control, so `CreateWindowL` builds one of those and
lends the game its window: old `Window()` is a single load from offset 0x20, so
putting the window there is the whole of it.

With that, the game gets through its control's `ConstructL` and into what it
wanted the screen for. ws32's old ordinals 348 and 350 -- past the end of every
list we have -- read out of the ROM as a four-argument factory returning a
0x60-byte object and a starter for it: `CDirectScreenAccess::NewL` and
`StartL`, which 9.x still has. The game hands the first the session and screen
device from its `CCoeEnv`, its window, and itself as the abort observer.

### Where the calling conventions finally disagree

`TParseBase::DriveAndPath()` is where the ABI difference the toolchain spike
left open actually bit. GCC98r2 returned an eight-byte structure in r0 and r1 --
the game stores both straight after the call -- and EABI returns anything over
four bytes through a hidden pointer in r0, pushing `this` to r1. So that import
gets a thunk that borrows eight bytes of stack, lets the callee fill them, and
hands them back in registers. The demangled names carry no return type, so the
functions that need it are listed by name rather than detected.

Three other things the game needed on the way:

- `TInt64` was a class of two words in EKA1 and a plain `long long` in 9.x,
  which exports nothing. Constructing or assigning one from a `TInt` is a sign
  extension, generated locally.
- `CApaApplication::DllName()` is how the game finds its own data files. Our
  application is a 9.x `CEikApplication` built from nothing and has no name to
  give, so the answer is the path the loader actually opened the game from.
- cone's old ordinal 318, another export past the end of our lists, stands in
  as `SetRect`: in the ROM it adjusts a size against something keyed by UID
  0x101f8a5a, and the game calls it on its control with the rectangle
  `ApplicationRect` just returned.

```
Thread Gate6 panicked with category: G6IMP and exit code: 464048
```

Import 48: `bluetooth.dll` ordinal 9. The game is past its own screen setup and
into the N-Gage's networking.

One thing to come back to: the window server logs `Can't find requested screen`
with a garbage number while direct screen access is being set up, and takes the
focused screen instead. It does not stop anything yet, but it means something
we hand `CDirectScreenAccess` is not being read the way it expects.

## The N-Gage subsystems, and the end of ConstructL

`bluetooth.dll` ordinal 9 -- the next stop -- reads out of the N-Gage ROM as a
constructor for a 0x28-byte object with a six-byte address at offset 0x10, and
the game builds twenty-three of them, each followed by a `TBuf<256>`. A
discovered-device list. That is not one call to answer, it is a subsystem.

Between them, ARENAFRAMEWORK, BLUETOOTH, GAMECOMMS, GAMEUTILS and NOKIAFC are
the N-Gage's multiplayer and arena services: 32 imports that no S60v3 phone
answers at all. They now all get the same stand-in -- do nothing, return zero --
rather than one panic at a time. A GCC98r2 call site keeps the pointer it
allocated rather than the one a constructor hands back, so a constructor losing
its return value costs nothing; what it does cost is that everything built this
way stays empty, which is the point. Single player first.

Key click sounds went the same way. `CAknAppUi::KeySounds()` is inline in 9.x
and reads a field of `CAknAppUiBase`, which our app UI -- a `CEikAppUi` -- does
not have, so it answers with nothing; the context push it feeds has to be
stubbed with it, since that one is called on whatever the first returns.

With those, the game's `ConstructL` runs to the end:

```
Thread Gate6 panicked with category: G6CHN and exit code: 4
```

Every line of it. Base construction, the game card check, its own control, the
window, direct screen access, the telephony watcher it uses to notice incoming
calls, the control stack, key block mode, key sounds. So the panic came out and
`ConstructL` now returns, and the framework runs the application: it goes on to
load `avkonfep.dll` and enter the event loop, which is as far as an application
gets before it is asked to draw.

One emulator bug fell out of this. The game sets the key block mode before
anything else has made EKA2L1's UI server initialise, and
`update_key_block_mode` dereferenced its `eik_server` without checking -- the
sibling accessor `get_sgc_server()` initialises on first use and this one did
not. Fixed in the emulator rather than worked around here.

## What the phone was actually dying of

The observer was real and had to be fixed, but it was not the fault. Two
things found it.

First, visibility. EKA2L1's JIT was crashing in host code with unreadable
frames, which said nothing; its interpreter does not, and the configuration
key is `cpu`, not `cpu_backend` -- `cpu: dyncom` in `config.yml`. With that,
faults come out as guest register dumps again. (Its own remaining crash, in
the C++ exception path while loading `avkonfep.dll`, looks like an emulator
gap rather than ours: a leave unwinding through `__cxa_allocate_exception`
with a null where it wants a pointer.)

Second, the game's own code. `CTimer::CTimer(TInt)` at 0x39090 and
`CActiveScheduler::Add` at 0x392b8, immediately after the direct screen
access it sets up: the game's graphics object **is a CTimer**, and it hands
itself to the active scheduler. Its vptr is GCC98r2, eight bytes before slot
0. When the first timer expires, the scheduler calls `RunL` through it with
the 9.x layout and jumps into the vtable header. That is exactly a fault the
moment after `ConstructL` returns, which is what the phone reports.

So `CActive` is the fifth object that needs the two-graph treatment, and the
most demanding one, because the framework does not merely call it -- it owns
its request. The crossings are all visible in the import list:
`CTimer::CTimer`, `CTimer::ConstructL`, `CTimer::After`,
`CActiveScheduler::Add`, `CActive::SetActive` and `CActive::Cancel`. Each is
called by the game on its own object and has to be given a 9.x `CActive` of
ours instead, so that the request completes into the wrapper's `iStatus` and
the scheduler calls the wrapper's `RunL`, which dispatches into the game's
the old way. What the game reads back out of its own `CActive` -- the
completion code above all -- has to be copied across at that point, which
needs both layouts measured out of the two ROMs, the way `CCoeEnv`'s were.

### The timer wrapper

The fifth two-graph object, and the first the framework owns rather than
merely calls. Both vtables were read out of their own ROM:

```
9.x   0,1 ~CTimer   2 CBase::Extension_   3 DoCancel  4 RunL  5 RunError
7.0s  0   ~CTimer   1 DoCancel            2 RunL      3 RunError
```

`CActive::Cancel` confirms each side: the 7.0s one calls through `[vptr+0xc]`,
which with the GCC98r2 bias is slot 1, and the 9.x one through `[vptr+0xc]`,
which without it is slot 3. The extra 9.x slot is `CBase::Extension_`, which
7.0s did not have. The game's own vtable has exactly four entries, all four
overridden.

The data needs no translation at all, which is the good luck in this one.
`CActive` keeps `iStatus` at 4 and `iActive` at 8 in both. 9.x turned `iActive`
into a flag word, but bit zero is what `ETrue` sets, so the old code's writes
still mean what they did; `CActive::SetActive` tests exactly that bit. What
grew is `iLink` at the end, which only the framework touches -- `CTimer`'s own
member moved from 0x18 to 0x1c, and that is the four bytes of it.

So a 9.x `CTimer` is constructed alongside the game's, and everything the game
does to its own -- `ConstructL`, `After`, `CActiveScheduler::Add`, `SetActive`,
`Cancel` -- is diverted to ours, so the request completes into the wrapper's
`iStatus` and the scheduler calls the wrapper's `RunL`. That copies the status
and the active flag back into the game's object and dispatches into its slot 2
the old way.

The emulator cannot judge this one: it still stops earlier, in its own C++
exception path while loading the FEP.

### What three rounds of guessing were actually chasing

The timer wrapper did not clear the fault, and nor did the observer. A probe
that stops at the first `RunL` never fired, so the scheduler never reached the
timer at all, and another that faults on purpose with an exception handler
installed came back as `KERN-EXEC 3` -- so a phone does not dispatch to a user
handler either, and there is no debugger to be had. That is worth knowing
rather than assuming, but it left only one instrument: a panic at a point of
our choosing.

So, enumerate instead. Every import the game calls that hands the framework
something of its own, found by pattern over the import list and confirmed at
the call site. There were nine, and eight were accounted for. The ninth:

```
00039280  add r0, sp, #8      @ an RThread on the stack
00039284  ldr r1, [pc, #0x188] @ the handler
00039288  mvn r2, #0           @ the mask
0003928c  bl  <import 359>     @ RThread::SetExceptionHandler
```

9.x moved that from `RThread` to `User`, and the shim forwarded it as an
ordinary call -- so `User::SetExceptionHandler` received the `RThread` as its
handler and the handler as its mask. A non-null handler at a nonsense address,
with every exception class enabled, installed midway through `ConstructL`:
after that, the first exception of any kind jumps into nothing, and what it
was originally is unknowable.

The fix is a third argument shape alongside the two already there: drop `this`
and move the rest down a register.

## Why the emulator cannot judge this, and what it did show

Working in the emulator instead of on the phone meant finding out why the
emulator stops. Three things came out of it.

**The official S60v3 build of the same game is installed here and runs.** That
makes a control: whatever the emulator does to us, it does not do to a normal
application.

**The emulator's crash is its own.** Our application asks for the front-end
processor; `avkonfep.dll` had been renamed to `.bak` in this ROM, so the load
leaves with `KErrNotFound`, and the leave is taken by the real C++ throw
machinery in `euser`. That reaches `__cxa_allocate_exception`, which calls
`__cxa_get_globals` -- two instructions, a `UserSvr::DllTls` and nothing else --
and gets null, because drtaeabi's per-thread exception state is never
initialised for an application thread. Tracing every `DllTls` call confirms it:
`dll_tls h=0x8018fe08 -> 0x0`, immediately before the fault, in our thread and
in the control's alike. The control never trips it because it never throws.

Putting the FEP back moves the failure rather than fixing it: the FEP now loads
and its own initialisation leaves with `KErrNoMemory`, which throws just the
same. The same null, the same instruction. So the emulator will stop at the
first leave that has to become a C++ exception, whatever causes it, and none of
that happens on a phone, where the FEP is present and the runtime is
initialised.

**Two things were ruled out on the way.** Our image's flags are identical to a
shipped S60v3 executable's -- `0x1200002a`, `KImageNoCallEntryPoint` and all --
so that is not it. And the static call list for our process had six entries
against the official build's twenty-four, because a normal EABI application
links the C++ runtime and ours did not: it only ever loaded drtaeabi at run
time, through the shim. One static reference fixes that, and it is right
whether or not it matters here.

## The black box

A phone reports a fault as KERN-EXEC 3 and will not dispatch one to a handler,
so a trace cannot escape the crash. It can survive it. Every import already
goes through a thunk that records which one it was; that thunk now calls a
function that also writes the number to a file on E: every sixteenth call, and
the next run reads it back, reports it as a panic and deletes it. Two launches,
one answer.

The first attempt put the write on a timer, which never fired: the fault
happens while the framework is still starting the application and the active
scheduler has not run yet, so nothing of ours gets a turn. Moving the write
into the trace itself fixed that -- and the timer stays, for a fault that
happens once the scheduler is going, where the last import may be a long way
back.

Proved in the emulator before being sent anywhere, which is what the emulator
is still good for even though it cannot run the application past its own first
leave. Run one leaves `lastImport = 25`; run two reports it. Import 25 is
`CAknAppUi::KeySounds()`, the second-to-last call the game's `ConstructL`
makes -- exactly where it should be, since nothing of the game's runs after
that in the emulator.

The phone said the same thing: 25, and no game code after `ConstructL` at all.
So the fault is in framework code working on our objects, and the next thing
to record is which of them it touches. Every slot of every wrapper's vtable --
application, document, app UI, control, timer -- now goes through a thunk that
notes the object and the slot and carries on. The report carries three fields:
the last slot of ours the framework called, the last import the game made, and
which callbacks had fired.

In the emulator that reads `G6BOX 77945700`: object 3 slot 11, import 457,
nothing else. Slot 11 of a 9.x `CEikAppUi` is
`HandleApplicationSpecificEventL`, which is also the check that the slot
numbering is right -- slot 16 of the same table is `ConstructL`, which is the
one the wrapper has been patching all along.

### Versioning the record

The first build of the slot recorder reported `2500` on the phone -- exactly
what the build before it had left behind. A record from an older build reads
back as a plausible number and says nothing: the file was eight bytes in the
old format, the new build read twelve, and the third field came back zero.

So the file now carries what wrote it, and a record from anything else is
ignored. It is also re-armed by being replaced rather than deleted, since a
delete the file server refuses would leave the same stale record to be reported
again on every launch afterwards -- which is the shape of what happened.

Proved by planting an old-format record and running: it is ignored, the file is
re-armed, the run records, and the next run reports it.

## The phone and the emulator are the same failure

With the record versioned and the stale file gone, the phone reported
`G6BOX 77945700` -- the emulator's number exactly. Both stop with the same last
framework call into our objects and the same last import from the game's. The
earlier conclusion that the emulator's crash was its own was wrong; it is the
same failure, and the emulator can be worked in after all.

The number says: the last thing the framework called on us was the app UI's
slot 11, which the ROM's own `_ZTV9CEikAppUi` names
`HandleApplicationSpecificEventL`. So the framework is dispatching window
server events to our app UI when it dies -- and our app UI was only a
`CEikAppUi`, while everything around it is avkon's, which reaches past the
virtuals into `CAknAppUi`'s own members.

### Making it a CAknAppUi

Neither `CAknAppUi`'s constructor nor its vtable can be linked against by name,
but `CAknAppUiBase`'s constructor is exported and the vtable is an export like
any other. Both are looked up at run time and checked before use -- no offset
to a containing object, every slot filled -- since the ordinals come from a
source release later than any phone.

With that, and with the flags the game actually asks for:

```
Thread Gate6 panicked with category: CONE and exit code: 14
```

`ECoePanicNoResourceFileForId`. A named panic from the framework rather than a
fault, and it is asking for something specific: `CAknAppUi::BaseConstructL(0)`
wants the status pane and menu resources of a real application resource file,
and ours holds a caption and nothing else.

Asking avkon for less is worse rather than safer. With
`ENoAppResourceFile | ENoScreenFurniture` -- what the `CEikAppUi` wrapper used
-- a `CAknAppUi` goes off the rails entirely and never gets a virtual called at
all. So the resource file is the next thing to build, not something to avoid.

### The application resource file

`CONE 14` was literal: the UI framework finds `EIK_APP_INFO` by position, as
the third resource of the application's resource file, and ours had two --
the signature and the caption. A shipped S60v3 file has six; the four that are
read are the signature, a document name, the app info, and the caption, so the
caption moves from second to fourth and the registration resource points at its
new id.

`EIK_APP_INFO` itself is six resource links -- hotkeys, menu bar, toolbar,
toolband, status pane, command buttons -- and a word the compiler adds. All
zero asks avkon for its defaults, which is what an application with no menus of
its own wants. The shipped file's is twenty-eight bytes of zeros; so is this.

With that, `CAknAppUi::BaseConstructL(0)` gets through, and the window server
starts taking draw commands from us. What stops it now is the emulator's own
gap again: a leave that has to become a C++ exception, and drtaeabi's
per-thread state that was never set up.

## The C++ runtime's per-thread state

The thing that had been reported as KERN-EXEC 3 for five rounds, and that the
emulator was blamed for, was ours.

On 9.x a leave *is* a C++ exception -- `TRAP` is `try`/`catch` -- so every leave
goes through `__cxa_allocate_exception`, which asks `__cxa_get_globals` for the
thread's exception state. In Symbian's runtime that function is two
instructions: a `UserSvr::DllTls` and a return. Nothing allocates lazily. The
state is created once per thread, and the tracing showed who does it: the only
caller of drtaeabi's TLS setter is `TCppRTExceptionsGlobals`'s constructor,
which is exported -- because euser calls it. `callfirstprocessfn.cpp` in the
kernel source:

```cpp
TInt CallThrdProcEntry(TInt (*aFn)(void*), void* aPtr, TInt aNotFirst)
    {
    TCppRTExceptionsGlobals aExceptionGlobals;
    ...
```

A local, on the entry function's frame, whose constructor does
`Dll::SetTls(this)`. Our startup is our own -- `UserHeap::SetupThreadHeap`,
`User::InitProcess`, our main -- and never constructed one. So
`__cxa_get_globals` returned null, and the first leave that had to become an
exception read through it.

That is why the fault was always in framework code with nothing of ours or the
game's anywhere near it, why the phone and the emulator agreed exactly, and why
the control application never tripped it: it has euser's thread entry, and it
never had to throw either.

The fix is one call, before anything can leave, on a heap block rather than a
stack frame so it outlives the work. With it, leaves unwind: thirty-eight of
them pass in a run where the second used to be fatal, and the application gets
as far as the game's own control construction before the next thing stops it --
a virtual call through a null vptr from inside cone, which is a different
question.
