# ADX12 Source

This directory contains the directly browsable ADX12 implementation, not a
generated binary, cache, or abbreviated patch listing.

`dxmt-adx12/` is a complete export of the tracked files at downstream commit
`cd4450687ae075cb15d42c87e7d5a3a1ab764079`. It starts from DXMT commit
`e55ad281c60be97f8815b5848e57cfd9f967d759` and includes every ordered ADX12
change in `dependencies/patches/dxmt/`. The export contains the D3D12 and DXGI
runtime, DXMT and WineMetal implementation, public and native headers, build
definitions, tests, and tools needed by that source revision.

The source remains governed by its retained upstream license and copyright
notices, including `dxmt-adx12/LICENSE` and `dxmt-adx12/COPYING.LIB`. The root
repository's `GPL-3.0-only` license applies to project-authored ADX12 material;
it does not replace third-party terms.

The two DXMT-owned nested dependencies are represented by their original
gitlinks inside the snapshot:

- `external/nvapi` at `d08488fcc82eef313b0464db37d2955709691e94`;
- `include/native/directx` at `9df86f2341616ef1888ae59919feaa6d4fad693d`.

Run `scripts/verify-visible-dxmt-source.sh` to validate the snapshot's pinned
identity, downstream markers, patch queue, nested dependency declarations,
machine-neutral path policy, and all 378 exported source blobs against
`ADX12_SOURCE_SHA256SUMS`.
