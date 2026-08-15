# Source layout

- `core/` contains the emulator lifecycle, 65C02 execution loop, memory map,
  RAM, and shared state.
- `devices/` contains the emulated NC2000 hardware: NAND/NOR, keyboard, LCD,
  sound, UART, and SoC I/O.
- `desktop/` contains the SDL desktop entry point, settings, command console,
  disassembler UI, and UDP bridge.

Target-specific code stays outside this directory. In particular,
`platform/bbk9588/` contains the BDA frontend, MIPS32 JIT, freestanding
runtime, audio backend, and device integration for the BBK 9588.
