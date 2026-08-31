# D3DMetal Semantic Gap Plan

## Role

D3DMetal is a required semantic oracle for macOS-specific gaps that remain
after consulting the Microsoft contract and vkd3d-proton semantic donor. It is
not the ADX12 backend, is not linked into release builds, and is not
redistributed by this repository.

The same deterministic D3D12 test must be runnable through three independent
profiles:

```text
test executable
    +-> D3DMetal installed reference runtime
    +-> unmodified DXMT baseline
    +-> ADX12 candidate runtime
```

This comparison answers two different questions:

- vkd3d-proton shows mature D3D12 behavior across Vulkan implementations;
- D3DMetal shows how Apple's own D3D12 runtime maps that behavior onto the
  current macOS, Metal, and Apple GPU environment.

Neither implementation is assumed correct merely because it succeeds. The
Microsoft specification remains the authority; the comparison isolates where
ADX12 or DXMT behavior needs investigation.

## Audited Local Profiles

The architecture audit found two independently installed, Apple-signed
D3DMetal profiles. Their binaries are not copied into the repository.

| Distribution | Framework version | Binary SHA-256 |
| --- | --- | --- |
| CrossOver Apple GPTK runtime | `3.0` | `05a7beaed4494a4f5f53d3f626a82fffc3b70146436a908b7048a0632a49e1a8` |
| Apple evaluation environment | `3.0b5` | `e058d61c89a901169ac2cf85c71081fbd0771649e791c5f04c8a84a8516bb1ba` |

Both observed binaries are x86_64 frameworks intended for the Wine/Rosetta
evaluation environment and declare macOS 14.0 as their minimum operating
system. Test reports must record the selected D3DMetal version, binary hash,
macOS build, Wine build, Metal device name, and GPU family.

The framework exports the standard D3D12 and DXGI entry points and identifiers
for modern interface revisions. It also contains symbols associated with
ExecuteIndirect, mesh dispatch, ray tracing, and work graphs. Symbols prove
surface presence only; `CheckFeatureSupport` and behavioral tests determine
whether a feature is actually admitted.

## Discovery Contract

No committed script may contain a developer-specific application path. The
future test runner will accept:

```text
ADX12_D3DMETAL_ROOT=/path/to/D3DMetal.framework
ADX12_D3DMETAL_PROFILE=reference-name
```

If the variables are absent, discovery may search known product-relative
locations without recording the user's home directory. Failure to find an
installed framework skips D3DMetal comparison tests; it must not fail the open
source ADX12 build.

Before a run, the harness must verify:

1. the framework identifier is `com.apple.D3DMetal`;
2. the binary is code-signed and its selected architecture is runnable;
3. the version and SHA-256 fingerprint are recorded;
4. no framework file is copied into the build or result archive;
5. the user has accepted the license terms of the product that installed it.

## Differential Harness

Each semantic test emits a machine-readable record with the same schema for
all three profiles:

- DLL and adapter discovery result;
- requested and returned COM interface identifiers;
- HRESULT and device-removal result;
- complete `CheckFeatureSupport` input and output bytes;
- root-signature and pipeline creation outcome;
- descriptor, resource, heap, and alignment values;
- submitted queue/list type and monotonic fence timeline;
- normalized debug messages;
- buffer readback or image hash;
- process exit, timeout, crash, and device-loss classification.

The runner must execute each profile in a fresh process. It must never swap
D3D12 implementations inside a process because loader state, COM singletons,
shader caches, and Metal objects would contaminate the comparison.

## Gap Resolution Order

### 1. Loader and COM surface

- `D3D12CreateDevice`, `D3D12GetInterface`, root-signature serializers, and
  DXGI factory behavior;
- `QueryInterface` coverage through current device, command-list, fence,
  resource, and state-object revisions;
- reference counting, private data, names, and destruction order.

### 2. Capability reporting

- feature-level admission and every `CheckFeatureSupport` structure;
- shader model, root-signature, binding, tiled-resource, conservative-raster,
  queue, format, architecture, and cross-node queries;
- fail-closed behavior for unavailable Metal or Apple GPU features.

### 3. Resources, heaps, and descriptors

- allocation sizes and alignments;
- placed, committed, reserved, shared, and simultaneous-access resources;
- descriptor increment sizes, null descriptors, view compatibility, and
  descriptor-copy overlap;
- residency, aliasing, sparse/tiled mapping, and destruction while in flight.

### 4. Commands and synchronization

- command allocator/list reset rules;
- legacy and enhanced barriers;
- queue ordering, split transitions, UAV hazards, aliasing, and ownership;
- bundles, ExecuteIndirect, predication, queries, timestamps, fences, waits,
  signals, and device loss.

### 5. Pipelines and shaders

- root-signature reflection and compatibility;
- pipeline-state stream validation and caching;
- DXIL validation, system values, resource binding, wave operations, atomics,
  memory model, and precision behavior;
- graphics, compute, mesh, amplification, and ray-tracing pipeline results.

### 6. Presentation and recovery

- swapchain formats, color spaces, HDR, frame latency, resize, occlusion, and
  fullscreen-emulation behavior;
- drawable loss, device removal, application teardown, and recovery.

## Classification Rules

Every disagreement receives one classification:

| Classification | Meaning | Action |
| --- | --- | --- |
| `ADX12_BUG` | ADX12 disagrees with the specification and references | Fix ADX12 and add regression coverage |
| `DXMT_GAP` | Baseline DXMT lacks behavior that D3DMetal/reference implements | Adapt the smallest compatible semantic unit |
| `D3DMETAL_QUIRK` | D3DMetal differs from the public D3D12 contract | Add an opt-in compatibility quirk only for affected applications |
| `BACKEND_LIMIT` | Metal or the current Apple GPU lacks a required capability | Report the lower tier or feature level |
| `UNRESOLVED` | Evidence is insufficient or contradictory | Reduce the test before changing production code |

No result is promoted from `UNRESOLVED` by guessing a private object layout.

## Reverse-Engineering Escalation

D3DMetal testing starts with public ABI calls, Metal validation, GPU capture,
Metal System Trace, normalized logs, and output comparison. LLDB or Ghidra is
used only when all of the following are true:

- the failing behavior is reduced to a deterministic test;
- Microsoft and Apple public contracts do not explain it;
- DXMT, vkd3d-proton, Wine vkd3d, Mesa, and KosmicKrisp source do not already
  provide a reusable answer;
- the question is limited to object lifetime, parameter meaning, state
  transition, or performance behavior needed for interoperability;
- no signing, entitlement, protected-code, or platform-security boundary is
  bypassed.

The retained result is a behavior contract and regression test. Decompiled
Apple implementation text, private binaries, proprietary shader libraries,
and private framework resources are not committed or shipped.

## Integration Rule

D3DMetal closes semantic uncertainty, but ADX12 remains independently
executable:

```text
D3DMetal evidence
        |
        v
minimal semantic contract + regression test
        |
        v
DXMT-derived ADX12 implementation
        |
        v
dxil-spirv + Mesa NIR + KosmicKrisp + WineMetal
```

A semantic gap is complete only when ADX12 passes the reduced test without
loading D3DMetal. D3DMetal may remain installed for comparison, but deleting it
must not break ADX12 compilation, loading, rendering, or presentation.

## First D3DMetal Pass

1. Add a test manifest and fresh-process profile runner.
2. Add framework fingerprinting without hard-coded machine paths.
3. Run loader, adapter, device, interface, and feature-query probes through all
   three profiles.
4. Produce the initial `DXMT_GAP` table from normalized differences.
5. Select the smallest high-confidence gap and fix it in the DXMT-derived
   runtime with a standalone regression test.

Exit condition: one real DXMT semantic gap is identified through D3DMetal,
fixed independently in ADX12, and verified against the Microsoft contract.
