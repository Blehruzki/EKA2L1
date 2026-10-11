ENTRY(_start)
SECTIONS {
  . = BASE;
  .text : { *(.text._start) *(.text*) }
  .rodata : { *(.rodata*) *(.data.rel.ro*) }
  /* Writable statics get a section of their own so build.py can refuse them:
     the image declares no data section, so they would sit in read-only code
     and fault on the first write on a phone. */
  .data : { *(.data*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) *(.bss*) }
}
