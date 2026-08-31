# Workstream Freeze

## DX12 Freeze Window

ADX12 implementation work in `Microsoft_AppleDrivers` is frozen from
2026-08-31 through 2026-09-07. Work may resume on 2026-09-08, or earlier only
when the project owner explicitly lifts the freeze.

During this window:

- keep the ADX12 source, downstream DXMT patch queue, runtime contract, tests,
  and packaging read-only;
- do not update the pinned DXMT, Wine/CrossOver, D3DMetal, or compiler inputs;
- record newly discovered issues without changing the frozen implementation;
- permit only an explicitly requested critical security or data-loss repair;
- do not describe ADX12 as abandoned, superseded, or Feature Level 12_2 ready.

OpenGL in `Khronos_AppleICDs` is the active implementation workstream during
this freeze. Vulkan remains outside this DX12 checkpoint unless the project
owner explicitly changes priorities.

## Frozen Checkpoint

- DXMT baseline: `e55ad281c60be97f8815b5848e57cfd9f967d759`
- final downstream DXMT commit: `cd44506`
- downstream patchset: `6f10828e9e85cc4e6f61a5d9c69068bbf529b558d564f528d04ecc2c9cc44718`
- runtime ABI: `2`
- runtime directory identity: `runtime-e55ad281-adx12-6f10828e`
- current capability ceiling: D3D12 userspace bring-up, not Feature Level 12_2

Verified at the checkpoint:

- native D3D12/DXGI and source-built WineMetal runtime packaging;
- compute dispatch with root CBV, SRV, UAV, root constants, barriers, fences,
  and exact readback on the active Apple Silicon/CrossOver test system;
- committed and placed buffer alias handoff;
- placed RGBA8 texture alias upload, barrier handoff, copy, and exact 16 KiB
  readback;
- root-signature bounds and normalized multi-binding root-constant metadata;
- frontend environment, resource-heap, root-signature, upstream-sync, checksum,
  package-provenance, and machine-neutral path checks.

Still intentionally incomplete:

- a hardware workload consuming multiple independent root-constant blocks;
- broad texture formats, subresources, views, resolves, and enhanced barriers;
- general graphics pipelines, presentation, queries, and complete D3D12 state;
- Feature Level 12_2 semantic coverage and conformance qualification.

## Restart Check

Before ADX12 work resumes:

1. Confirm that the pinned upstream revisions still exist and review upstream
   movement without mutating the checkpoint.
2. Replay patches `0001` through `0017` from the recorded DXMT baseline and
   verify the resulting tree against downstream commit `cd44506`.
3. Rebuild runtime patchset `6f10828e...` and verify package checksums and
   machine-neutral metadata.
4. Re-run frontend, root-signature, resource-heap, CrossOver admission, and
   compute/readback tests.
5. Resume with multi-block root-constant hardware execution and generalized
   texture/subresource aliasing; do not skip directly to a 12_2 capability
   claim.
