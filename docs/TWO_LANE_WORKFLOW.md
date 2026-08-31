# ADX12 Two-Lane Workflow

## Ownership Rule

ADX12 does not independently recreate D3D12 semantics already present in
vkd3d-proton. The pinned vkd3d-proton source is the primary semantic donor and
test corpus. DXMT remains only as the shortest proven native Metal object and
execution seam. Vulkan objects and dispatch do not enter the ADX12 runtime.

Each implementation pass advances both lanes when their dependency ordering
allows it and reports this checklist with `[x]`, `[~]`, and `[ ]` states.

## Lane 1 - Frontend and D3D12 Semantics

- [x] Isolated Wine/CrossOver process and loader environment.
- [x] Matched native `d3d12.dll` and `dxgi.dll` plus builtin WineMetal bridge.
- [x] Module provenance, compiler ABI, fallback rejection, and COM smoke tests.
- [x] Materialize the pinned vkd3d-proton semantic donor and test corpus.
- [x] Complete the initial directly reusable, backend-separable, and
  Vulkan-inseparable source classification.
- [x] Extract the first root-signature semantic unit and its tests.
- [x] License project-authored material under `GPL-3.0-only` while preserving
  the licenses and notices of reused LGPL and other third-party components.
- [~] Root-signature serialization/deserialization, versioned round-trip,
  mixed-table validation, and the 64-DWORD cost boundary are proven; the full
  vkd3d-proton root-signature matrix remains.
- [ ] Reuse COM identity, private-data, lifetime, parent, and interface rules.
- [~] Buffer and 2D-texture descriptors, committed-heap restrictions, one/many
  allocation sizing, and placed-resource heap class/alignment/range validation
  are proven from the vkd3d-proton corpus. Bounded default 2D textures now use
  real Metal placement heaps at exact offsets. Placed buffers now do the same,
  retain their parent heap, and pass a same-offset alias handoff through two
  blit encoders, an explicit D3D12 aliasing barrier, queue submission, a live
  fence, and exact readback. Legacy aliasing, UAV, and transition barriers now
  establish conservative Metal encoder-ordering boundaries. Same-offset RGBA8
  placed textures preserve an exact 16 KiB pattern through upload, alias
  handoff, readback, and a live fence. Broader texture formats/subresources,
  split/enhanced barriers, reserved resources, views, broader heap/resource classes,
  and the full allocation matrix remain.
- [ ] Reuse command allocator/list, bundle, render-pass, and indirect semantics.
- [ ] Reuse legacy/enhanced barrier, queue, fence, query, and timestamp rules.
- [ ] Reuse pipeline-state validation, caching, and feature-query behavior.
- [ ] Complete DXGI factory, adapter, swapchain, and presentation semantics.
- [ ] Import the backend-independent vkd3d-proton tests into the ADX12 runner.
- [ ] Qualify Feature Levels 11_0, 11_1, 12_0, 12_1, then every 12_2 gate.

Lane 1 exit condition: applications observe mature D3D12 behavior derived from
the specification and vkd3d-proton, with no Vulkan runtime dependency.

## Lane 2 - Compiler and Metal Backend

- [x] DXC/DXIL to dxil-spirv to Mesa VTN/NIR to KosmicKrisp MSL route.
- [x] Content-addressed compute bundle with DXIL/MSL hash validation.
- [x] Reflected bounded root SRV/UAV/CBV/root-constant resources, independent
  buffers, dispatch, UAV/transition barriers, copies, one live D3D12 fence, and
  exact dual readback.
- [x] Reproducible, source-built D3D12/DXGI and matched WineMetal PE/Unix
  runtime, including mandatory `winebuild --builtin` postprocessing.
- [x] D3D12, DXGI, WineMetal PE, embedded Metal AIR, and WineMetal Unix outputs
  are path-neutral and pass the release metadata scan. The rebuilt matched
  runtime passes device creation, compute, fences, and exact hardware readback.
- [x] Replace loose per-field environment bundle handoff with the versioned,
  content-addressed `adx12-compiler-cache-v1` manifest ABI.
- [~] `populate-adx12-compiler-cache.sh` accepts arbitrary compute DXIL with
  1-16 strict root SRV/UAV/CBV/root-constant resources, validates SPIR-V reflection and Metal
  compilation, and atomically publishes a content-addressed manifest.
  Transparent runtime cache-miss invocation and broader reflected resource
  classes remain.
- [x] Flatten overlapping D3D12 SRV/UAV register namespaces into deterministic,
  unique SPIR-V bindings before Mesa VTN/NIR while preserving each reflected
  D3D12 root-parameter class in the compiler manifest.
- [~] Reflected root SRVs/UAVs/CBVs/root constants are generalized to 1-16
  bindings and a mixed SRV/UAV/root-constant workload passes hardware. Samplers,
  descriptor tables/arrays, and dynamic indexing remain. Multiple root-constant
  declarations are normalized and validated by the exporter; a multi-block
  hardware workload remains before that subpath is fully proven.
- [ ] Define backend contracts for resources, descriptors, barriers, and queues.
- [ ] Route vertex, pixel, geometry, amplification, and mesh stages through KK.
- [ ] Complete graphics PSOs, render targets, depth/stencil, textures, and draws.
- [ ] Implement Metal mappings for enhanced barriers and multi-queue ordering.
- [ ] Complete residency, sparse/tiled resources, recovery, and device loss.
- [ ] Complete WineMetal swapchain presentation, resize, HDR, and frame latency.
- [ ] Implement Metal-backed VRS, sampler feedback, mesh, WaveOps, and SM 6.5.
- [ ] Implement DXR 1.1 only after the general compute/graphics backend is solid.

Lane 2 exit condition: the reused D3D12 semantic layer executes through a
general Mesa/KosmicKrisp compiler path and native WineMetal backend.

## Reuse Classification

`Direct reuse` covers code whose behavior and storage are backend-neutral,
including serializers, parsers, validation helpers, COM utilities, and tests.

`Semantic extraction` covers code where the D3D12 state machine is reusable
but the final operation currently creates or submits Vulkan objects. ADX12
retains the behavior and calls a Metal backend contract instead.

`Reference only` covers algorithms whose assumptions are fundamentally Vulkan,
such as descriptor-buffer layout tied to Vulkan limits or command generation
that requires Vulkan extensions. These remain test or design evidence.
