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

## Not done yet

- **Imports.** `mke32.py` writes no import section, so nothing can be called
  yet.  This needs real EUSER ordinals, which means gate 2.
- **Reading stock images.** E32 code is compressed with Symbian's own deflate
  (`0x101F7AFC`), which is not zlib; reading the import section of a shipped
  binary needs that inflater ported.
