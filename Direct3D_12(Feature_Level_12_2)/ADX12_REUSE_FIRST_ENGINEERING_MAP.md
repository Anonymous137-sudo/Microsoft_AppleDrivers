# ADX12 Reuse-First Engineering Map

## Audit Scope

This map is the implementation companion to
`ADX12_Extensive_Project_Architecture_and_BringUp.pdf`. The complete 88-page
document was reviewed line by line before this map was written. The PDF remains
the project genesis and design rationale. This map supersedes implementation
choices in the PDF where current upstream projects now provide more complete
machinery than the original plan assumed.

Audit baseline: 2026-08-24.

Reviewed source snapshots and installed comparison profiles:

| Project | Reviewed revision | Intended ADX12 role |
| --- | --- | --- |
| DXMT | `e55ad281c60be97f8815b5848e57cfd9f967d759` | Native D3D12/DXGI and Wine-to-Metal baseline |
| vkd3d-proton | `d01924b6f02fa3f78e3d371269267355363dbcb7` | Primary D3D12 semantic donor and compatibility test corpus |
| Wine vkd3d | `fb7f9ff3c1f8d972ee6f1b8c6a92caf6c8ba1e10` | ABI/shader reference and differential tests |
| dxil-spirv | `9d71d9679506abecd9399f82945c5df1259edba4` | Bootstrap DXIL parser and DXIL-to-SPIR-V lowering |
| Mesa | `4641f0094f29752f2774e5c0cbfc75d5c76a2f26` | VTN, NIR, optimization, and shared compiler utilities |
| DirectX-Headers | `ee479f0bd5f7b884f202bcf0c3f076cc050dd256` | Canonical API definitions and version tracking |
| D3D12TranslationLayer | `708c27ee207356d19a12cb23520ac83496dc5192` | Allocation, batching, and residency reference only |
| Wine | `8da89f8493b21ebfbe344a54dbef0cde23c7ea59` | Windows ABI, PE loader, and Unix-call host environment |
| DirectXShaderCompiler | `9a3e225dab9683212c648d2cc2b7f09bbf57cbe8` | DXIL production and validation tooling |
| Apple D3DMetal | `3.0` and `3.0b5` installed profiles | Required macOS semantic oracle for DXMT gap closure; never vendored |

These revisions are immutable dependency pins managed by
`dependencies/upstreams.lock.tsv`. Weekly candidate updates are reviewed and
verified before the lock changes; upstream source is materialized locally and
is not copied into this repository by default.

## Corrected Architecture

```text
Windows Direct3D 12 application
        |
        v
Wine / CrossOver PE and Unix boundary
        |
        v
ADX12 d3d12.dll / d3d12core.dll / DXGI integration
        |
        v
vkd3d-proton-derived D3D12 contracts and validated semantics
        |
        v
DXMT-derived Metal-native object and execution seam
        |                         |
        |                         +-> resources, heaps, descriptors
        |                         +-> queues, command lists, fences
        |                         +-> pipelines, queries, swapchain
        v
DXIL -> dxil-spirv -> SPIR-V -> Mesa VTN/NIR
        |
        v
KosmicKrisp NIR-to-MSL lowering and Apple shader policy
        |
        v
WineMetal native Metal execution and presentation
        |
        v
Public Metal API -> Apple graphics stack -> AGX
```

The same reduced D3D12 tests run separately against D3DMetal, unmodified DXMT,
and ADX12. D3DMetal is the required Apple-platform semantic oracle described in
[`D3DMETAL_SEMANTIC_GAP_PLAN.md`](D3DMETAL_SEMANTIC_GAP_PLAN.md); it is not a
runtime dependency in the architecture above.

ADX12 is not a WDDM kernel driver and does not replace the macOS kernel GPU
stack. It is also not a call-by-call wrapper. D3D12 objects retain D3D12
lifetime, ordering, descriptor, barrier, and synchronization semantics while
Metal is the execution API below that semantic boundary.

## Architecture Decisions

### Retained from the genesis specification

- Keep the application-facing D3D12/DXGI ABI separate from compiler and Metal
  execution code.
- Preserve D3D12-visible semantics instead of exposing Metal objects to the
  application.
- Keep capability reporting fail-closed and test-gated.
- Use deterministic traces and differential testing against mature
  implementations.
- Treat Feature Level 12_2 as a target whose complete capability contract must
  be met before it is reported.
- Use D3DMetal to expose and prioritize DXMT semantic gaps on the same macOS,
  Metal, and Apple GPU stack, then implement each proven behavior independently
  in ADX12.
- Keep AVK143 outside the native runtime. It may be a separate comparison path,
  never an invisible ADX12 dependency.

### Amended after source audit

- Do not hand-author the initial D3D12 COM runtime. Current DXMT already
  implements the correct-direction D3D12/DXGI-to-Metal object path.
- Do not build a second Metal object system beside WineMetal. WineMetal owns
  Wine bridging, Metal devices, resources, queues, command encoding, and
  presentation.
- Do not create an ADX12-specific command IR during bring-up. Extend DXMT's
  existing command representation until a measured limitation proves a new IR
  is necessary.
- Do not create a custom `ADXIL` parser while dxil-spirv and Mesa VTN cover the
  required input. A new parser is allowed only for a demonstrated semantic or
  performance gap that cannot be fixed upstream.
- Make vkd3d-proton the primary semantic donor instead of independently
  completing known D3D12 behavior in DXMT. Directly reuse backend-neutral
  serializers, validation, COM behavior, utilities, and tests where licensing
  permits. Extract separable state-machine behavior behind ADX12 backend
  contracts. Do not copy its `Vk*` storage or dispatch into the native Metal
  runtime.
- Retain DXMT only where it already supplies the shortest proven native Metal
  path: the D3D12/DXGI object seam, WineMetal bridge, Metal resources,
  execution, synchronization, and presentation. Replace or correct its
  D3D12-visible decisions with vkd3d-proton behavior rather than inventing
  parallel semantics.
- Do not run both the KosmicKrisp Metal bridge and WineMetal as competing
  runtime backends. KosmicKrisp owns NIR-to-MSL lowering; WineMetal owns native
  Metal execution. Targeted MTL4 concepts may be ported only when they replace,
  rather than duplicate, WineMetal behavior.

### Deferred or rejected

- A backend-neutral ADX12 command stream solely to switch between Metal and
  Vulkan is deferred. Differential validation can run the same application or
  test through separate ADX12 and vkd3d-proton DLL configurations.
- A private AGX submission backend is outside the bring-up plan. The public
  Metal path is the supported execution contract.
- Shipping, embedding, or dynamically requiring D3DMetal is outside the open
  source runtime plan. It remains an optional installed test oracle whose
  absence cannot break ADX12.
- D3D12TranslationLayer is not an implementation base because it translates in
  the opposite direction and is currently MSVC-oriented.
- Wine vkd3d's experimental MSL target is reference material, not the production
  shader compiler. Its own source marks that target unsupported.

## Component Ownership

| Responsibility | Reuse owner | ADX12 work |
| --- | --- | --- |
| PE exports, COM ABI, D3D12/DXGI object skeleton | DXMT plus Wine ABI headers | Product naming, loader policy, missing interfaces, conformance fixes |
| D3D12 semantic implementation | vkd3d-proton plus Microsoft specifications | Extract backend-neutral behavior and adapt Vulkan-coupled decisions to explicit Metal backend contracts |
| macOS-specific D3D12 semantic evidence | Apple D3DMetal installed runtime | Three-way test profiles, normalized evidence, and independent DXMT gap fixes |
| Metal-native object/execution seam | DXMT D3D12 and WineMetal | Keep proven object ownership and execution; replace incomplete visible semantics with vkd3d-proton-derived behavior |
| Wine/native Metal boundary | DXMT WineMetal | Reproducible build, capability gates, targeted MTL4 modernization |
| DXIL parsing and initial lowering | dxil-spirv | Stable adapter, diagnostics, shader cache keys, unsupported-feature policy |
| SPIR-V to NIR and generic optimization | Mesa VTN/NIR | Pin compatible APIs and maintain downstream integration tests |
| NIR to MSL | KosmicKrisp | D3D12 system-value, descriptor, memory-model, and feature lowering gaps |
| HLSL/DXIL production and validation | DXC | Build/test tool integration; no required runtime dependency |
| Canonical API and Feature Level definitions | DirectX-Headers and DirectX-Specs | Generate an auditable capability matrix |
| Native Metal execution and presentation | WineMetal and public Metal | Missing scheduling, residency, swapchain, HDR, and recovery behavior |
| Driver validation | vkd3d-proton tests, Wine tests, Microsoft samples | macOS runner, baselines, failure minimization, release reports |

## What We Must Build

The source audit narrows original ADX12 code to real gaps:

1. A reproducible superbuild and dependency lock that can build the selected
   DXMT, Mesa, KosmicKrisp, dxil-spirv, and Wine-facing components together.
2. An ADX12 product/loader boundary around the reused D3D12 and DXGI runtime,
   including clean `d3d12.dll` and `d3d12core.dll` discovery under Wine.
3. A shader adapter that replaces DXMT's current SM5-only DXBC/AIR route for
   D3D12 DXIL with dxil-spirv, Mesa VTN/NIR, and KosmicKrisp-generated MSL.
4. Correct binding conversion among D3D12 root signatures/descriptors,
   dxil-spirv descriptor metadata, NIR resources, MSL argument tables, and
   WineMetal resource residency.
5. A semantic extraction layer that reuses vkd3d-proton's completed validation,
   state-machine, root-signature, descriptor, barrier, command, query, and
   feature behavior while replacing its Vulkan storage/dispatch operations with
   explicit ADX12 Metal backend contracts.
6. A capability database that reports only features proven on the current
   Metal device family and operating-system profile.
7. A three-way differential and native test harness covering D3DMetal,
   unmodified DXMT, and ADX12 across COM behavior, shader translation, command
   ordering, rendering, compute, presentation, and teardown.
8. Feature-specific implementations that do not already exist in the reused
   stack, progressing through honest Feature Level gates.

## Feature Level 12_2 Gate

ADX12 must not report `D3D_FEATURE_LEVEL_12_2` until all required capabilities
are implemented and behaviorally tested. The official contract includes at
least:

- Shader Model 6.5;
- DXR Tier 1.1;
- Variable Rate Shading Tier 2;
- Mesh Shader Tier 1;
- Sampler Feedback Tier 0.9;
- Resource Binding Tier 3 and Root Signature 1.1;
- Tiled Resources Tier 3;
- Conservative Rasterization Tier 3;
- depth bounds, WaveOps, logic ops, Int64 shader operations, typed-format
  casting, copy-queue timestamps, and the specified GPU virtual-address
  guarantees.

The Windows-only WDDM requirement is not something a macOS userspace runtime
can literally provide. ADX12 must document how Wine's userspace contract
substitutes for OS/DDI assumptions and must not claim Microsoft certification.

Current audited DXMT code hard-codes Feature Level 11_0 and has incomplete
D3D12 interfaces. It is a valuable implementation baseline, not proof of
Feature Level 12_2.

## Blocker Escalation Policy

Every blocker follows the same decision path:

1. **Public contract:** check Microsoft, Wine, Mesa, and Apple specifications
   and public APIs.
2. **Existing implementation:** search DXMT, vkd3d-proton, Wine vkd3d,
   dxil-spirv, Mesa, and KosmicKrisp for reusable behavior.
3. **Controlled experiment:** reduce the problem to a deterministic test and
   compare D3DMetal, unmodified DXMT, ADX12, and where appropriate
   vkd3d-proton.
4. **Targeted inspection:** use symbols, logs, captures, LLDB, or Ghidra only
   for the smallest opaque correctness or performance question that remains.
5. **New implementation:** write ADX12-owned code only after the prior stages
   establish that no suitable reusable implementation exists.

Reverse engineering is evidence gathering, not the default architecture. It
must not be used to bypass code-signing, entitlement, protected-executable, or
other platform-security boundaries. Findings are recorded as contracts and
tests; decompiled implementation text is not copied into project source.

## Revised Bring-Up Plan

### Phase 0 - Architecture and licensing

- [x] Audit the complete genesis architecture.
- [x] Inventory current reusable upstream implementations.
- [x] Select the reuse-first runtime, compiler, and backend boundaries.
- [x] License project-authored material under `GPL-3.0-only`, compatible with
  direct reuse of LGPL components when their notices and obligations remain
  intact.
- [x] Record dependency pins, source provenance, patches, and update policy.

Exit condition: the project can legally and reproducibly consume its selected
baseline. `GPL-3.0-only` is the selected project license. DXMT and
vkd3d-proton retain their LGPL terms; their notices, corresponding-source
requirements, and other applicable obligations remain intact.

### Phase 1 - Build the existing native baseline

- [x] Establish reproducible upstream pins and non-loss patch/overlay update
  automation.
- [x] Establish the Wine/CrossOver ADX12-only renderer admission contract.
- [x] Materialize a reviewed DXMT baseline without committing local build
  output.
- [x] Build stripped native D3D12/DXGI targets on Apple Silicon from the pin and
  ordered downstream patch queue; stage them with the pinned WineMetal bridge.
- [ ] Rebuild the WineMetal Unix module and full support-target set locally when
  its pinned LLVM/Airconv toolchain is qualified.
- [x] Load the exact pinned DXMT baseline under an isolated CrossOver bottle.
- [x] Run adapter enumeration, Feature Level 12_2 device creation, direct queue
  creation, and teardown against the staged baseline.
- [ ] Run the same loader/device probe through an installed, fingerprinted
  D3DMetal profile and record normalized differences.

Exit condition: a reproducible unmodified baseline succeeds before ADX12
patches are introduced.

### Phase 2 - Establish the shader path

- [x] Compile one DXIL compute shader through dxil-spirv and Mesa VTN/NIR.
- [x] Lower the resulting NIR through KosmicKrisp to MSL.
- [x] Create a WineMetal compute pipeline and verify buffer output.
- [x] Add cache identity, reflection, validation, and deterministic failure
  diagnostics.
- [x] Prove DXMT `CreateComputePipelineState`, root UAV binding, command-list
  dispatch, queue signal, D3D12 fence wait, copy, and mapped readback through
  the application-facing ABI.
- [x] Route one content-addressed Mesa/KosmicKrisp compute bundle through DXMT
  `CreateComputePipelineState` and adapt one root UAV to the KK descriptor ABI.
- [x] Replace loose external bundle fields with a versioned, content-addressed
  compiler-cache manifest.
- [ ] Populate that cache on demand and generalize reflection-driven
  roots/descriptors beyond the bounded workload.

Bounded exit condition: the admitted compute workload executes through the
intended compiler and Metal path without DXBC/AIR fallback and rejects a
hash-mismatched bundle. General Phase 2 completion still requires the compiler
service and non-fixed descriptor layouts.

### Phase 3 - Graphics and presentation

- [ ] Route vertex and pixel DXIL through the same compiler path.
- [ ] Connect root signatures, descriptors, render targets, and pipeline state.
- [ ] Execute clear, triangle, texture, depth, and readback tests.
- [ ] Present through DXGI/WineMetal with resize, loss, and teardown coverage.

Exit condition: a visible D3D12 sample renders and presents deterministically.

### Phase 4 - Semantic completeness

- [ ] Replace conservative or ignored barrier behavior with proven resource
  state and synchronization semantics.
- [x] Prove bounded same-offset buffer and RGBA8 texture alias-content handoff
  through explicit legacy alias barriers and live Metal-backed fences.
- [ ] Complete command lists, bundles, indirect execution, queries, fences,
  heaps, residency, and device-loss behavior.
- [ ] Run shared tests against ADX12, DXMT, D3DMetal, and vkd3d-proton and
  classify every delta.

Exit condition: the implementation passes its admitted feature-level and API
behavior suites without capability over-reporting.

### Phase 5 - Feature Level progression

- [ ] Admit and test 11_0, then 11_1.
- [ ] Admit and test 12_0, then 12_1.
- [ ] Implement the complete 12_2 capability matrix, including SM 6.5, mesh,
  VRS, sampler feedback, sparse/tiled resources, and DXR 1.1.

Exit condition: each reported level is backed by a published machine-neutral
test report. Feature Level 12_2 remains unchecked until every mandatory gate
is complete.

## First Implementation Pass

Do not add empty ADX12 object scaffolding. The first implementation pass is:

1. preserve the selected `GPL-3.0-only` repository license and all applicable
   third-party notices;
2. add a machine-readable dependency lock with the audited source revisions;
3. build the unmodified DXMT D3D12/WineMetal baseline;
4. retain its smoke result as the regression floor;
5. only then introduce the DXIL-to-NIR-to-KosmicKrisp integration branch.

## Primary References

- [Microsoft Feature Level 12_2 specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12_FeatureLevel12_2.html)
- [Microsoft DirectX-Headers](https://github.com/microsoft/DirectX-Headers)
- [DXMT](https://github.com/3Shain/dxmt)
- [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton)
- [Wine vkd3d](https://gitlab.winehq.org/wine/vkd3d)
- [dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv)
- [Mesa](https://gitlab.freedesktop.org/mesa/mesa)
- [D3D12TranslationLayer](https://github.com/microsoft/D3D12TranslationLayer)
- [Apple Metal feature set tables](https://developer.apple.com/metal/capabilities/)
- [Apple game porting and Windows evaluation environment](https://developer.apple.com/games/game-porting-toolkit/)
