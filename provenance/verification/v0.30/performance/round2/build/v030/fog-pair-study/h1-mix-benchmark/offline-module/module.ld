ENTRY(h1_module_entry)
SECTIONS {
 . = 0x81700000;
 __module_base = .;
 .text : { KEEP(*(.text.h1_module_entry)) *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) *(.sdata*) }
 . = ALIGN(16); __module_image_end = .;
 .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(16); }
 __module_memory_end = .;
 ASSERT(__module_memory_end - __module_base <= 65536, "Module exceeds RAM arena")
 /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags*) *(.reginfo*) *(.eh_frame*) }
}
