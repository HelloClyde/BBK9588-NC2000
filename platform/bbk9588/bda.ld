OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(bda_entry)

SECTIONS
{
  . = 0x81c00020;

  .entry 0x81c00020 :
  {
    KEEP(*(.text.bda_entry))
  }

  .text : ALIGN(4)
  {
    *(.text .text.*)
  }

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4)
  {
    *(.data .data.*)
    *(.sdata .sdata.*)
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    KEEP(*(SORT(.ctors.*)))
    KEEP(*(.ctors))
    __init_array_end = .;
  }

  .bss (NOLOAD) : ALIGN(8)
  {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  }

  /DISCARD/ :
  {
    *(.comment)
    *(.note .note.*)
    *(.pdr)
    *(.reginfo)
    *(.MIPS.abiflags)
    *(.eh_frame .eh_frame.*)
  }
}

ASSERT(bda_entry == 0x81c00020, "BDA entry address changed")
