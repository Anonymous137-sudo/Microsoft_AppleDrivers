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

- [`source/dxmt-adx12`](source/dxmt-adx12) contains the complete, directly
  browsable assembled D3D12, DXGI, DXMT, and WineMetal source used by the
  current checkpoint. It is not hidden behind the patch queue or an ignored
  developer checkout. [`source/README.md`](source/README.md) records its exact
  upstream/downstream provenance and retained licensing.
- [`third_party`](third_party) exposes every accepted external semantic,
  compiler, header, and ABI dependency as a pinned gitlink. The lock manifest
  remains the reproducibility authority; the gitlinks make those exact source
  revisions inspectable from the repository UI.
- [`Direct3D_12(Feature_Level_12_2)`](Direct3D_12(Feature_Level_12_2)/README.md)
  contains the ADX12 architecture, public contracts, and focused probes.
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
- Keep implementation source and accepted dependency revisions directly
  inspectable. Ignored directories are restricted to generated builds,
  downloaded toolchains, caches, traces, and temporary materializations.

## Source And Build

The default runtime build consumes the committed visible source:

```sh
./scripts/verify-visible-dxmt-source.sh
./scripts/build-adx12-dxmt-runtime.sh
```

An explicit clean development checkout may still be selected with
`ADX12_DXMT_SOURCE=/path/to/dxmt`, but it must contain the accepted upstream
ancestor and every ordered ADX12 patch. External dependency source can be
initialized with `git submodule update --init --recursive`. Generated runtime
artifacts and toolchains remain outside Git because they are reproducible build
outputs, not missing implementation source.

## Independence

This is an independent engineering project. It is not affiliated with,
sponsored by, or endorsed by Microsoft, Apple, Khronos, Valve, CodeWeavers,
Wine, Mesa, or the maintainers of any referenced project. Product and project
names belong to their respective owners.

## Licensing

Project-authored material is licensed under the
[GNU General Public License version 3](LICENSE), identified as
`GPL-3.0-only`. Third-party dependencies and adapted source retain their own
licenses, copyright notices, and redistribution requirements; those terms are
tracked separately and are not replaced by the repository-level license.
