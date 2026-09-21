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

## Not done yet

- **Calling convention.** APCS and AAPCS disagree about 64-bit arguments and
  struct return, and nothing has exercised either.
- **Reading stock images.** E32 code is compressed with Symbian's own deflate
  (`0x101F7AFC`), which is not zlib; reading the import section of a shipped
  binary needs that inflater ported.
