ENTRY(_start)
SECTIONS {
  . = BASE;
  .text : { *(.text._start) *(.text*) }
.rodata : { *(.rodata*) *(.data.rel.ro*) }
  /* Writable statics get a section of their own, and build_gate6.py fails the
     build if anything lands here. This image declares data size and bss zero,
     so a writable static would be placed in the read-only code segment and
     every write to it faults on hardware. Folding .data into .rodata hid that
     completely: build 52 shipped a one-character patch of a static and the
     phone took KERN-EXEC 3 before it could create a file, while the emulator,
     which maps the segment writable, ran twelve launches without complaint. */
  .data : { *(.data*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) *(.bss*) }
}
