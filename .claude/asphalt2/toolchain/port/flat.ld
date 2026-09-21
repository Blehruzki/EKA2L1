ENTRY(_start)
SECTIONS {
  . = BASE;
  .text : { *(.text._start) *(.text*) }
  .rodata : { *(.rodata*) *(.data.rel.ro*) *(.data*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) *(.bss*) }
}
