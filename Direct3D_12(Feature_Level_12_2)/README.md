# ADX12

ADX12 is the planned Direct3D 12 userspace driver/runtime in the
`Microsoft_AppleDrivers` workspace. It targets macOS on Apple Silicon and
Direct3D Feature Level 12_2.

## Status

`Phase 2 - bounded Mesa/KosmicKrisp compute route integrated`

The pinned and downstream-patched DXMT source now supplies native D3D12/DXGI
entry points and WineMetal execution under a hash-verified, isolated CrossOver
environment.
The Target 1 smoke creates and tears down DXGI factory/adapter and D3D12
device/queue objects while proving COM identity and rejecting alternative
renderers. A bounded Shader Model 6 compute workload now passes through pinned
DXC, dxil-spirv, Mesa VTN/NIR, KosmicKrisp's production descriptor lowering,
and WineMetal to real Metal completion and exact buffer readback. The same MSL
is admitted by application-facing `CreateComputePipelineState` only when its
DXIL and MSL hashes match, and command-list submission converts a reflected,
bounded 1-16 root-SRV/UAV/CBV/root-constant set to the KK descriptor ABI. A deterministic bridge
flattens D3D12's overlapping SRV/UAV/CBV register namespaces into unique SPIR-V
bindings while preserving root-parameter classes in the compiler manifest.
The integrated D3D12 dispatch, UAV barrier, transition, copy, fence, and
readback route returns
`12,15,18,21` and `19,25,31,37`; a deliberately wrong MSL hash is rejected.
The first vkd3d-proton-derived resource slice also proves committed buffer and
2D-texture descriptor validation, one/many allocation-info sizing, and placed
resource heap class/alignment/range rejection. Bounded default 2D textures now
use real Metal placement heaps at exact offsets; same-offset alias creation,
nonzero placement, overflow rejection, and resource-owned heap lifetime pass on
hardware. Placed buffers also use exact Metal heap offsets, and a same-offset
alias handoff passes upload, an explicit D3D12 aliasing barrier, readback, and
fence verification. A same-offset RGBA8 placed-texture alias also preserves an
exact 16 KiB upload across the alias barrier and fenced readback. Legacy
aliasing, UAV, and transition barriers now end the active Metal encoder to
establish submission ordering. Broader texture alias formats/subresources,
split-transition state tracking, enhanced barriers, and finer hazard scopes remain.
The complete D3D12/DXGI/WineMetal runtime is now
rebuilt from pinned source, Wine-builtin postprocessed, machine-neutral, and
hardware-tested without the path-bearing upstream WineMetal binaries. A
reusable cache command now accepts compute DXIL with 1-16 strict root SRV/UAV/CBV
resources and
an explicit normalized map of reflected root-constant binding/count contracts,
then atomically
publishes a validated content-addressed manifest. Transparent
cache-miss compilation, general descriptors/root signatures, graphics stages,
and wider D3D12 feature semantics remain. The directory name is an engineering
target, not a Feature Level 12_2 compatibility claim.

## Active Architecture

```text
D3D12 application under Wine / CrossOver
        |
        v
D3D12 + DXGI ABI frontend
        |
        v
DXMT-derived D3D12 and DXGI runtime
        |
        +---- existing resources, heaps, descriptors, queues, commands
        +---- ADX12 correctness fixes and missing feature implementation
        |
        v
DXIL -> dxil-spirv -> SPIR-V -> Mesa VTN/NIR -> KosmicKrisp MSL
        |
        v
DXMT WineMetal execution and presentation
        |
        v
Metal / AGX
```

The D3D12 runtime owns application-visible Direct3D semantics. Metal remains
the native execution API rather than becoming the public object model.
vkd3d-proton is the primary open-source semantic donor and test corpus:
backend-neutral code is reused directly and separable state-machine behavior
is adapted to explicit Metal contracts. Its Vulkan storage and dispatch are
not part of the ADX12 runtime. AVK143 is not a native runtime dependency.
Apple D3DMetal is additionally used as the required macOS-specific semantic
oracle for closing proven DXMT gaps. D3DMetal is not linked, copied, or shipped
as part of ADX12.

## Architecture Specification

[`ADX12_Extensive_Project_Architecture_and_BringUp.pdf`](ADX12_Extensive_Project_Architecture_and_BringUp.pdf)
is the project genesis, original architecture, validation strategy, and initial
roadmap.

[`ADX12_REUSE_FIRST_ENGINEERING_MAP.md`](ADX12_REUSE_FIRST_ENGINEERING_MAP.md)
is the current implementation authority. It records the complete 88-page
architecture audit and supersedes original implementation assumptions where
current DXMT, dxil-spirv, Mesa, KosmicKrisp, or other audited projects already
provide mature machinery.

[`D3DMETAL_SEMANTIC_GAP_PLAN.md`](D3DMETAL_SEMANTIC_GAP_PLAN.md) defines the
three-way D3DMetal/DXMT/ADX12 differential workflow, artifact fingerprinting,
gap classifications, legal/runtime boundary, and first semantic-gap pass.

[`TARGET_1_FRONTEND.md`](TARGET_1_FRONTEND.md) defines the active frontend
work: Wine/CrossOver supplies only the Windows execution environment, while
ADX12's matched native `d3d12.dll`/`dxgi.dll` pair and builtin PE/Unix
`winemetal` bridge remain the sole renderer entry path. Exact hashes and
runtime paths establish ownership. WineMetal stays builtin only because its PE
thunk owns the Unix-call bridge; no WineD3D, DXVK, vkd3d-proton, or CrossOver
D3DMetal renderer is admitted.

[`../../docs/PHASE2_SHADER_PATH.md`](../../docs/PHASE2_SHADER_PATH.md) records
the exact DXIL-to-MSL ownership, KosmicKrisp root descriptor ABI, deterministic
cache/reflection checks, WineMetal hardware readback, and reproduction command.

[`../../docs/TWO_LANE_WORKFLOW.md`](../../docs/TWO_LANE_WORKFLOW.md) defines
the mandatory per-pass frontend/semantics and compiler/Metal checklists.

## Current Engineering Gate

Invoke the established cache population automatically on runtime misses, then
extend the proven bounded root-SRV/UAV/CBV/root-constant ABI to samplers,
descriptor tables, dynamic root layouts, and graphics stages. Continue the vkd3d-proton
resource sequence with texture alias handoff, split/enhanced barriers,
general views, broader formats, and reserved resources. Project-authored work
is licensed under `GPL-3.0-only`; direct DXMT/vkd3d-proton reuse must preserve
the applicable upstream LGPL terms, notices, and source obligations.
