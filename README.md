# Microsoft_AppleDrivers

`Microsoft_AppleDrivers` is the workspace for ADX12, an independent,
work-in-progress Direct3D 12 userspace implementation for macOS on Apple
Silicon. ADX12 is frozen at a reproducible engineering checkpoint from
2026-08-31 through 2026-09-07 while OpenGL work resumes in the separate
`Khronos_AppleICDs` project.

The project is not a WDDM driver and is not intended to be a thin,
call-by-call D3D12-to-Metal wrapper. The intended architecture exposes the
Direct3D 12 and DXGI ABI expected by Windows applications running through
Wine or CrossOver, owns the application-visible D3D12 semantics in its own
runtime, compiles shaders through a controlled DXIL/NIR pipeline, and uses
Metal as a native execution backend.

```text
Windows Direct3D 12 application
        |
        v
Wine / CrossOver
        |
        v
ADX12 D3D12 + DXGI ABI
        |
        v
vkd3d-proton-derived D3D12 semantics
        |
        v
DXMT-derived Metal-native runtime seam
        |
        v
DXIL -> dxil-spirv -> Mesa VTN/NIR -> KosmicKrisp
        |
        v
WineMetal native Metal execution
        |
        v
Metal / Apple GPU
```

## Project

- [`Direct3D_12(Feature_Level_12_2)`](Direct3D_12(Feature_Level_12_2)/README.md)
  contains the ADX12 architecture and will contain the implementation.
- The
  [reuse-first engineering map](Direct3D_12(Feature_Level_12_2)/ADX12_REUSE_FIRST_ENGINEERING_MAP.md)
  records the complete architecture audit, current dependency ownership, exact
  blockers, capability gates, and revised bring-up sequence.
- The
  [D3DMetal semantic-gap plan](Direct3D_12(Feature_Level_12_2)/D3DMETAL_SEMANTIC_GAP_PLAN.md)
  makes Apple's installed D3DMetal runtime a required differential oracle for
  DXMT gaps without making it a linked or redistributable ADX12 dependency.
- [`docs/WORKSTREAM_FREEZE.md`](docs/WORKSTREAM_FREEZE.md) records the one-week
  ADX12 freeze, exact runtime/patchset checkpoint, known gaps, and restart
  checks.
- [`docs/UPSTREAM_SYNCHRONIZATION.md`](docs/UPSTREAM_SYNCHRONIZATION.md)
  documents immutable dependency pins, downstream patch preservation, and the
  weekly review-PR updater.
- [`docs/TWO_LANE_WORKFLOW.md`](docs/TWO_LANE_WORKFLOW.md) is the active pass
  checklist. Lane 1 imports vkd3d-proton D3D12 semantics into the frontend;
  Lane 2 generalizes the Mesa/KosmicKrisp compiler and WineMetal backend.
- [`docs/VKD3D_PROTON_REUSE_INVENTORY.md`](docs/VKD3D_PROTON_REUSE_INVENTORY.md)
  classifies the pinned semantic donor into direct-reuse, semantic-extraction,
  and Vulkan-inseparable areas.
- The current repository state is **Phase 2: bounded modern compute path
  integrated**. A stripped native DXMT-derived `d3d12.dll`/`dxgi.dll` pair and
  the pinned builtin WineMetal bridge are admitted through an isolated,
  hash-verified CrossOver host. Pinned DXC, dxil-spirv, Mesa VTN/NIR, and
  KosmicKrisp produce deterministic MSL. `CreateComputePipelineState` admits
  the versioned content-addressed compiler-cache manifest, command-list binding
  adapts a mixed root SRV/two-UAV/root-constant set to the KK descriptor ABI,
  conservatively orders UAV and transition barriers at Metal encoder
  boundaries, and real
  dispatch/copy/fence/readback returns `12,15,18,21` and `19,25,31,37`. The
  route is reproducible from the downstream patch queue and
  rejects a mismatched MSL hash. Root-signature versioned round-trip and the
  exact 64-DWORD cost boundary are also regression-tested from vkd3d-proton's
  semantic corpus. Placed buffer and RGBA8 texture aliases preserve exact
  content across explicit alias barriers and live-fence readback. It is still
  one bounded compute contract, not
  a general D3D12 shader compiler or finished ADX12 release.
- Feature Level 12_2 is the target, not a current compatibility claim. The
  baseline's raw capability response is recorded but does not lift a gate.

## Design Rules

- Reuse vkd3d-proton as the primary D3D12 semantic donor and regression corpus;
  do not independently recreate behavior it already implements.
- Keep DXMT's proven D3D12/DXGI-to-WineMetal object and execution seam instead
  of replacing the native Metal path with a Vulkan backend.
- Keep the public D3D12/DXGI ABI, shader compiler, and Metal execution layers
  separated by explicit ownership boundaries.
- Preserve D3D12-visible behavior rather than reshaping it around Metal.
- Reuse mature open-source ABI, compiler, and reference machinery where its
  license and architecture permit; do not recreate established components for
  novelty.
- Keep backend-specific Metal objects below the ADX12 runtime boundary.
- Reuse dxil-spirv, Mesa VTN/NIR, and KosmicKrisp for the modern shader path;
  do not create a second DXIL parser or NIR-to-MSL compiler without a proven
  gap.
- Extract backend-neutral vkd3d-proton code directly where licensing permits,
  adapt separable behavior behind explicit Metal contracts, and compare reduced
  tests before writing behavior that no reused implementation supplies.
- Use Wine/CrossOver only as the Windows ABI/process environment. ADX12 must
  fail rather than fall back to WineD3D, DXVK, vkd3d-proton, D3DMetal, or
  Wine's Vulkan renderer in its native profile.
- Report capabilities only after the relevant implementation and deterministic
  tests exist.
- Treat AVK143 and other implementations as validation/reference paths, not as
  hidden dependencies of the native Metal backend.

## Independence

This is an independent engineering project. It is not affiliated with,
sponsored by, or endorsed by Microsoft, Apple, Khronos, Valve, CodeWeavers,
Wine, Mesa, or the maintainers of any referenced project. Product and project
names belong to their respective owners.

## Licensing

The project license has not yet been selected. Direct reuse of the audited DXMT
and vkd3d-proton code strongly favors LGPL-2.1-or-later; that decision must be
recorded before source is imported. Until a license is added, no
permission to copy, modify, or redistribute project-authored material is
granted beyond rights provided by applicable law. Third-party dependencies
will retain their own licenses and will be tracked separately before they are
introduced.
