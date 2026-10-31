ENTRY(h1_bda_main)
SECTIONS {
  . = 0x83c00020;
  .entry : { KEEP(*(.text.h1_bda_entry)) }
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.sdata*) }
  .got : { *(.got*) }
  . = ALIGN(16);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(.sbss*) *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }
  .stack (NOLOAD) : { . += 0x10000; __app_stack_top = .; }
  ASSERT(__app_stack_top <= 0x83ff0000,"Dota H1 exceeds application RAM window")
  ASSERT(h1_bda_main == 0x83c00020,"H1 entry moved")
  /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags*) *(.reginfo*) *(.eh_frame*) }
}
