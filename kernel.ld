/*
 * Ocean Kernel Linker Script
 *
 * Higher-half kernel at 0xFFFFFFFF80000000
 * Loaded by Limine at physical 0x100000
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Kernel virtual base address (higher half) */
KERNEL_VMA = 0xFFFFFFFF80000000;
/* Kernel physical load address */
KERNEL_LMA = 0x100000;

PHDRS {
    text    PT_LOAD FLAGS(5);   /* r-x */
    rodata  PT_LOAD FLAGS(4);   /* r-- */
    data    PT_LOAD FLAGS(6);   /* rw- */
}

SECTIONS {
    . = KERNEL_VMA;

    /* Store the kernel start address for later use */
    _kernel_start = .;

    /* Code section */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA) {
        _text_start = .;
        *(.text.boot)       /* Boot code first */
        *(.text .text.*)
        _text_end = .;
    } :text

    /* Limine requests section - must be kept with markers */
    .requests ALIGN(4K) : AT(ADDR(.requests) - KERNEL_VMA + KERNEL_LMA) {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :rodata

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } :rodata

    /* Read-write data */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } :data

    /* BSS (uninitialized data) */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } :data

    . = ALIGN(4K);
    _kernel_end = .;

    /* Calculate kernel size */
    _kernel_size = _kernel_end - _kernel_start;
    _kernel_phys_end = _kernel_end - KERNEL_VMA + KERNEL_LMA;

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
