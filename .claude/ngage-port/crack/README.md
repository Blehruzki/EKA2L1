# The BiNPDA loader, decoded

`binpda_6rbc.app` is the 3964-byte `6RBC.APP` from the second dump: a crack
*loader*, not a patched game. It loads `bin\main.dll` -- which is byte-identical
to the image this project runs -- and rewrites seven of its imports.

## How it patches

```
key  = crc32(bin\arenaframework.dll, 8508 bytes)
     + crc32(u"e:\system\apps\6rbc\bin\arenaframework.dll")   # 84 bytes
     + crc32(u"BiNPDA presents...")                           # 36 bytes
     + crc32(b"gt2 loader. (c) 2005 zg.")                     # 24 bytes
     (mod 2^32)
     = 0x85bbf5f4
```

then, for each of seven records at crack offset `0xb44`:

```
RDebug::WriteMemory(RThread::Id(),
                    RLibrary::EntryPoint() + (record.offset ^ key),
                    TPtrC8(&record.value, 4), 4)
```

`RDebug::Open(16, 16, 16, 0x10000)` first. The same two calls this port already
shims, because the game's own decryptor uses them.

## The seven patches

Offsets are into the loaded image; `(off - text_size) / 4` is the import index,
with `text_size = 1591740`. Every value is a pointer to a routine in the crack.

| import | code offset | what it was | replaced by | what the replacement is |
|---|---|---|---|---|
| 98 | `0x184b44` | ordinal 318 | crack+`0x18c` | `bx lr` -- returns at once |
| 101 | `0x184b50` | `RFile::Create(RFs&, const TDesC16&, TUint)` | crack+`0x2bc` | a function |
| 109 | `0x184b70` | `RFile::Open(RFs&, const TDesC16&, TUint)` | crack+`0x198` | a function |
| 111 | `0x184b78` | `RFile::Replace(RFs&, const TDesC16&, TUint)` | crack+`0x244` | a function |
| **326** | `0x184ed4` | **`RLibrary::Lookup(int) const`** | crack+`0x380` | a function, 1040 bytes of stack |
| 458 | `0x1850e4` | `CMdaAudioOutputStreamPadFunction` | crack+`0x190` | `b` to `CMdaAudioOutputStream::NewL` |
| 459 | `0x1850e8` | ordinal 1 | crack+`0x194` | `b` to an import stub |

## What that means

Two of the seven are a **compatibility** fix, not protection: the N-Gage build
imports an audio padding function and the crack points it at the real
`CMdaAudioOutputStream::NewL`.

The other five are the crack proper, and the important one is **import 326,
`RLibrary::Lookup`** -- which `gate6` already owns. The protection resolves its
own functions dynamically through `RLibrary::Lookup`, and the crack answers
those lookups itself. `RFile::Open`, `Create` and `Replace` are replaced too, so
it also stands in front of the game's file access.

`gate6` already intercepts `RLibrary::Lookup` and wraps `RFile::Open`. Carrying
this across is four routines of ARM code, not a new mechanism.
