OUTPUT_ARCH(mips)
ENTRY(test_all_current_bits)
SECTIONS { . = 0x81510000; .text : { *(.text*) *(.rodata*) } /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.debug*) } }
