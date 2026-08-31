# vkd3d-proton Semantic Reuse Inventory

Pinned source: `d01924b6f02fa3f78e3d371269267355363dbcb7`.

This inventory classifies implementation candidates. It does not grant a
license or copy source. The repository now uses `GPL-3.0-only`; direct source
reuse additionally preserves upstream LGPL terms, copyright notices, and
corresponding-source obligations.

## Direct-Reuse Candidates

- `libs/vkd3d/vkd3d_main.c`: root-signature serializers, deserializers, COM
  surfaces, and version conversion. The pinned file contains no `Vk*` symbols.
- `libs/vkd3d/heap.c`: heap-description and custom-heap validation. The pinned
  file contains no `Vk*` symbols, although device-capability inputs must be
  supplied by ADX12.
- `libs/vkd3d/bundle.c`: bundle recording and COM behavior. The pinned file
  contains no `Vk*` symbols; its callback targets require an ADX12 audit.
- `tests/d3d12_*.c`: application-facing D3D12 behavior tests covering devices,
  resources, descriptors, commands, synchronization, root signatures,
  graphics, compute, sparse resources, mesh shaders, VRS, sampler feedback,
  ray tracing, and work graphs.

## Semantic-Extraction Candidates

- `libs/vkd3d/resource.c`: reuse description, flags, dimensions, format,
  alignment, state, heap, map, descriptor, and query validation; replace Vulkan
  resource allocation and view creation with ADX12 backend operations.
- `libs/vkd3d/state.c`: reuse root-signature and pipeline validation, shader-I/O
  compatibility, blend/raster/depth rules, and PSO cache behavior; replace
  Vulkan pipeline/layout creation with KosmicKrisp and WineMetal operations.
- `libs/vkd3d/command.c`: reuse command-list state machines, allocator/reset
  rules, render-pass legality, barrier decisions, indirect validation, queue
  timelines, and fence behavior; replace Vulkan recording/submission with an
  explicit ADX12 command backend.
- `libs/vkd3d/device.c`: reuse COM surface, feature-query structure handling,
  format rules, and object creation validation; derive results from Metal/AGX
  capability records instead of Vulkan feature structures.
- `libs/vkd3d/queue_timeline.c`: reuse ordering and retirement model while
  replacing native Vulkan synchronization handles with WineMetal events and
  D3D12 fence storage.
- `libs/vkd3d/state_object_common.c`: reuse state-subobject association and
  validation logic while replacing backend shader/pipeline objects.
- `libs/vkd3d/raytracing_pipeline.c` and `workgraphs.c`: retain D3D12 parsing,
  validation, identifier, and scheduling concepts; Metal execution requires a
  separate advanced-feature backend.

## Vulkan-Inseparable Reference Areas

- Vulkan memory-type selection, `VkDeviceMemory` ownership, descriptor-buffer
  layouts, Vulkan pipeline caches, Vulkan render-pass construction, Vulkan
  swapchain ownership, and extension-specific fast paths are not copied into
  the Metal runtime.
- `VkCommandBuffer` emission, `vkQueueSubmit*`, Vulkan barriers, and Vulkan
  timeline semaphores are backend implementations, not reusable D3D12
  semantics. Their ordering decisions remain useful, but their operations are
  replaced by ADX12 backend contracts.
- Vulkan capability requirements do not become ADX12 capability claims. Every
  D3D12 feature is derived independently from public Metal capabilities and
  deterministic behavior tests.

## Initial Extraction Order

1. Import the backend-independent root-signature serializer/deserializer tests.
2. Compare vkd3d-proton root-signature validation with DXMT and replace missing
   behavior through a small ADX12 patch.
3. [In progress] The first committed-buffer descriptor, heap restriction, and
   allocation-info slice is imported and hardware-tested. Continue with placed
   buffers, textures, views, and complete allocation alignment behavior.
4. Define command-backend operations only after command-state and barrier
   semantics have been separated from Vulkan emission.
5. Generalize compiler reflection and descriptors in Lane 2 against the same
   root-signature model, preventing frontend and backend layouts from drifting.
