# Upstream and third-party notices

## NC2000

This repository is a BBK 9588 port derived from
[`wangyu-/NC2000`](https://github.com/wangyu-/NC2000). The port was based on
upstream commit `df10ea78f0056a742105f89a6184664f8dcc0f9d`. It is published as an
independent source snapshot with a new Git history rather than as a GitHub
fork. NC2000 is distributed under GNU GPL v3; see `LICENSE`.

Starting a new Git history does not remove or replace the copyright,
attribution, or license obligations of the imported upstream source.

The original project contains additional attribution to Sim800, Wayback800,
Pc1000emux, NC3000 emulator work, and the NC1020 emulator family. Those
credits remain applicable to the inherited core.

## wqxdsp

`dsp/dsp.cpp` and `dsp/dsp.h` are based on
[`wangyu-/wqxdsp`](https://github.com/wangyu-/wqxdsp), commit
`b50e3c39087c5fb7f09958130fe4b2a9069305d5`. The repository credits Lee as
the original DSP author and wangyu- for the `0xc2`, `0xc4`, and `0xc8`
command enhancements. Port-specific changes are documented in
`dsp/UPSTREAM.md`.

The pinned `wqxdsp` repository revision does not contain an explicit license
file or license grant. Attribution is not a substitute for permission. Before
publishing this source or a BDA containing the decoder, obtain redistribution
permission from the relevant rightsholders or replace/remove these files with
a compatibly licensed implementation.

## BBK 9588 SDK

`sdk/` is a Git submodule pointing to
[`HelloClyde/bbk9588-bda-sdk`](https://github.com/HelloClyde/bbk9588-bda-sdk),
currently at commit `ac4558a4c7d8ffbadf8de8f227cb9869e44d716c`. The SDK is licensed
separately under Apache License 2.0.

The BBK frontend and build workflow also reference the public porting patterns
in [`HelloClyde/gba-for9588`](https://github.com/HelloClyde/gba-for9588).

## Firmware and data

No NC2000/NC2600 firmware, NAND, NOR, dictionary data, BBK firmware, or device
software is part of this source repository. Such data remains subject to its
own rights and the notices published by the original NC2000 project.
