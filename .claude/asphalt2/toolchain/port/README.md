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

What remains of step 1 is the rest of the chain -- an application object, a
document and an app UI, each a 9.x-layout class whose vtable clang lays out and
whose base is constructed by the exported constructor, as the game's own
`NewApplication` does.

## Not done yet

- **The shim itself.** 462 stubs currently all panic. Each has to become a real
  implementation, or a forward to the S60v3 equivalent.
- **Calling convention.** APCS and AAPCS disagree about 64-bit arguments and
  struct return, and nothing has exercised either. Gate 4 is where that will
  show up, since the game's own code is now calling our functions.
- **Reading stock images.** E32 code is compressed with Symbian's own deflate
  (`0x101F7AFC`), which is not zlib; reading the import section of a shipped
  binary needs that inflater ported.
