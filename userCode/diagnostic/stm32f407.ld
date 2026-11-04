/* Local minimal linker script, no bootloader. STM32F407IG: 1 MiB FLASH, 128 KiB main SRAM. */
ENTRY(Reset_Handler)
_estack = 0x20020000;
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
         RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K }
PHDRS { flash PT_LOAD FLAGS(5); ram PT_LOAD FLAGS(6); bss PT_LOAD FLAGS(6); exidx 0x70000001 FLAGS(4); }
SECTIONS {
 .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) } > FLASH :flash
 .text : { . = ALIGN(4); *(.text*) *(.rodata*) KEEP(*(.buildinfo)) *(.glue_7*)
           KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
 .ARM.extab : { *(.ARM.extab*) } > FLASH
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH :flash :exidx
 .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH :flash
 .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH :flash
 .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH :flash
 _sidata = LOADADDR(.data);
 .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH :ram
 .bss (NOLOAD) : AT(ADDR(.bss)) { . = ALIGN(4); _sbss = .; __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = .;
     . = ALIGN(8); end = .; _end = .; . += 0x100; . += 0x1000; } > RAM :bss
 .ARM.attributes 0 : { *(.ARM.attributes) }
}
