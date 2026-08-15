# wqxdsp source

`dsp.cpp` and the decoder definitions in `dsp.h` come from
[`wangyu-/wqxdsp`](https://github.com/wangyu-/wqxdsp), commit
`b50e3c39087c5fb7f09958130fe4b2a9069305d5` (original DSP author credited
there as Lee).

The BBK9588 port replaces `std::vector` with a fixed 65,536-sample buffer and
uses integer arithmetic for the 12-sample WORD_MODE fades.  The CELP/PCM
decoder and SPDS104A command handling are otherwise unchanged.

## License status

The pinned upstream revision does not include an explicit license file or
license grant. Public redistribution of these files or their compiled output
requires permission from the relevant rightsholders, or replacement with a
compatibly licensed decoder. This record documents provenance only; it does
not assert that attribution alone grants redistribution rights.
