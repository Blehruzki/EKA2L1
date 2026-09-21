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

## Not done yet

- **Imports.** `mke32.py` writes no import section, so nothing can be called
  yet.  This needs real EUSER ordinals, which means gate 2.
- **Reading stock images.** E32 code is compressed with Symbian's own deflate
  (`0x101F7AFC`), which is not zlib; reading the import section of a shipped
  binary needs that inflater ported.
