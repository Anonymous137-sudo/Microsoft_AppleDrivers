# ADX12 Phase 2 Shader Path

Phase 2 proves the intended modern shader route without adding a second DXIL
parser, NIR implementation, NIR-to-MSL compiler, or Metal command bridge:

```text
HLSL compute shader
  -> pinned DirectXShaderCompiler (DXIL)
  -> pinned dxil-spirv (SPIR-V)
  -> Mesa VTN/NIR
  -> KosmicKrisp descriptor and NIR-to-MSL lowering
  -> content-addressed ADX12 compute bundle
  -> native DXMT-derived D3D12/DXGI runtime
  -> pinned WineMetal compute pipeline
  -> Metal command buffer and shared-buffer readback
```

## Proven Contract

The current bounded workload exposes one read-only byte-address SRV, two
write-only byte-address UAVs, and one four-DWORD root-constant block represented
as a uniform input at descriptor set
`0`, bindings `0` through `3`, and dispatches one `4 x 1 x 1` threadgroup.
KosmicKrisp owns descriptor lowering. The execution harness reproduces the
open-source KK ABI rather than rewriting generated MSL:

- each KK buffer descriptor is `{ uint64_t GPU address, uint32_t size,
  uint32_t zero }`, with a reflected contiguous table of 1-16 records;
- descriptor-set zero is stored at byte offset `928` in the `2240`-byte KK
  root descriptor table;
- the root table is bound at Metal buffer index `0`;
- indirectly addressed descriptor and output buffers are retained through the
  WineMetal command list;
- completion requires a real Metal command-buffer status of `Completed` and
  exact root-constant-dependent readback values `12,15,18,21` and
  `19,25,31,37`.

The Mesa change is maintained as
`dependencies/patches/mesa/0001-kosmicomp-add-ADX12-compute-descriptor-mode.patch`.
Together with `0002`, it adds bounded `kosmicomp --adx12-compute-ssbo-count`
support using KK's production
descriptor lowering and address formats. It does not introduce an ADX12
shader translator.

The matching DXMT changes are maintained as an ordered patch queue under
`dependencies/patches/dxmt`. The D3D12 seam:

- exports compiler ABI `2`, allowing tests to reject the host D3D12 module and
  proving support for the versioned compiler-cache manifest;
- accepts a bundle only after exact DXIL and MSL SHA-256 validation;
- validates the entry point, workgroup size, root-record ranges, and reflected
  root SRV/UAV/CBV/root-constant signature and root-constant DWORD count;
- compiles the admitted MSL into a Metal compute PSO;
- creates and retains the KK buffer descriptor and root record in DXMT's GPU
  heap through command submission;
- uses DXMT's existing command list, queue, fence, copy, and readback machinery.

If no ADX12 bundle is explicitly requested, upstream DXMT's Airconv path is
left unchanged. That fallback exists for unported workloads; it is not counted
as proof of the modern ADX12 compiler route.

## Reproduction

Build the hash-pinned compiler tools after installing their host build
dependencies:

```sh
scripts/build-adx12-shader-toolchain.sh
```

Build the patched native D3D12/DXGI runtime from the pinned source, ordered
patch queue, and hash-pinned Wine SDK:

```sh
scripts/build-adx12-dxmt-runtime.sh
```

The builder emits stripped native PEs with zero timestamps, rejects developer
paths in those binaries, and stages them with the pinned WineMetal PE/Unix
bridge. The runtime manifest records all source, patchset, SDK, loader-class,
and component-hash provenance without a host path.

With the Phase 1 CrossOver host and runtime present, run the full regression:

```sh
scripts/test-adx12-phase2-compute.sh
```

The regression performs two independent compilations and requires byte-exact
DXIL, SPIR-V, and MSL output. It validates SPIR-V, checks reflected stage,
threadgroup, descriptor binding, and access intent, compiles the MSL with the
Apple Metal compiler, and constructs a content-addressed cache identity from
all pinned components. It first keeps a direct WineMetal submission as an ABI
oracle, then runs the application-facing route against the same shader and
expected values:

1. Mesa/KosmicKrisp MSL is submitted directly through WineMetal with the KK
   root descriptor ABI.
2. The DXIL is submitted through DXMT's public D3D12 ABI. The matching
   Mesa/KosmicKrisp MSL bundle is admitted inside
   `CreateComputePipelineState`, the root SRV/UAV/root-constant set is lowered
   to KK descriptor records, and command-list `Dispatch`, UAV/transition
   barriers, default-to-readback copy, queue signal,
   `ID3D12Fence` wait, and mapped readback complete through the normal runtime.

Both observations return `12,15,18,21` and `19,25,31,37`. Negative controls
require `CreateComputePipelineState` to return `E_INVALIDARG` for either an
intentionally wrong MSL hash or correctly hashed root-CBV metadata paired with
an actual root-constant signature. Generated files stay
under ignored `.adx12-deps`; the manifest contains no developer-machine path.

## Remaining Boundary

The first application-facing route is integrated, but deliberately bounded to
one compute shader, 1-16 root SRV/UAV/CBV/root-constant resources, one descriptor-set layout, and fixed local
size. The generated MSL and reflection metadata are handed to the runtime
through one `adx12-compiler-cache-v1` manifest. The runtime validates its ABI,
schema, DXIL/MSL hashes, relative MSL filename, stage, workgroup, descriptor
contract, and KK root layout. It no longer accepts loose per-field shader
environment variables. Explicit reusable cache population is implemented;
transparent cache-miss invocation from the runtime remains.

The next boundary is generalization, not another handwritten shader engine:

- invoke the reusable DXC/dxil-spirv/Mesa/KosmicKrisp cache population command
  automatically when an application DXIL cache entry is absent;
- derive descriptor/root layouts from reflection rather than fixed offsets;
- support samplers, descriptor tables, dynamic roots,
  arrays, and access hazards;
- add graphics stages and pipeline state while preserving DXMT/WineMetal's
  existing resource, command, synchronization, and presentation machinery.
# Reusable Cache Population

`scripts/populate-adx12-compiler-cache.sh` accepts a DXIL compute shader, a
bounded root-resource count, and a cache root. An optional
`ADX12_ROOT_CONSTANTS=binding:dword-count[,binding:dword-count...]` contract
distinguishes one or more inline root-constant blocks from ordinary root CBVs.
The map is numerically normalized for deterministic cache identities and
rejects malformed, duplicate, out-of-range, unused, or non-uniform bindings. It
runs the pinned dxil-spirv and
Mesa/KosmicKrisp toolchain, verifies contiguous set-0 bindings and local size,
offline-compiles the generated MSL, and atomically publishes an
`adx12-compiler-cache-v1` directory keyed by all compiler pins and artifact
hashes. The application-facing D3D12 regression consumes this generated entry.
