# Target 1: Windows Frontend Environment

## Scope

Wine and CrossOver provide only the Windows process, PE loader, COM/Win32 ABI,
filesystem, registry, threading, and application environment needed to load
ADX12. They do not provide ADX12 rendering.

```text
Windows D3D12 application
        |
Wine / CrossOver environment only
        |
Hash-pinned native d3d12.dll + dxgi.dll, builtin winemetal
        |
DXMT-derived ADX12 runtime
        |
ADX12 shader and WineMetal execution path
```

The active process must not load WineD3D, DXVK, vkd3d-proton, D3DMetal, or
Wine's Vulkan renderer as an execution fallback. Those projects remain
independent references and differential profiles in separate processes.

## Implemented Foundation

- `frontend_environment.h` defines the stable environment-inspection ABI.
- `frontend_environment.c` distinguishes Wine and CrossOver without retaining
  bottle or user paths.
- The managed loader policy is `d3d12,dxgi=n;winemetal=b`. D3D12 and DXGI are
  a matched native PE pair. WineMetal stays builtin so its PE thunk reaches the
  paired Unix Metal module. Exact source pins, downstream patchset, runtime
  paths, and SHA-256 ownership distinguish them from host renderer modules.
  Fallback lists are rejected.
- Loaded-image inspection fails if a competing renderer is already present.
- `adx12-crossover-launch.sh` launches a selected bottle with
  `d3d12,dxgi=n;winemetal=b`, requires ADX12's paired Windows/Unix runtime,
  resolves the bottle-specific Windows path, places that runtime before
  CrossOver's DLL directories, and removes vkd3d/DXVK controls.
- Unit and probe targets verify the isolation policy before D3D12 objects are
  admitted.
- A Windows PE loader probe rejects modules outside the expected ADX12 runtime,
  records the actual `dxgi.dll`, `d3d12.dll`, and `winemetal.dll` paths,
  enumerates the adapter, creates a D3D12 device and direct queue, and records
  the admitted feature level.
- Factory, adapter, device, queue-parent, COM-identity, and teardown checks run
  through the same strict loader path.
- `package-adx12-frontend.sh` emits a machine-neutral baseline package with
  relative hashes, source revision, renderer policy, probe, and DXMT licenses.

CrossOver's `libd3dshared` is not rejected merely by name. The local CrossOver
launcher documents that it is present whether or not D3DMetal is enabled; the
guard rejects the D3DMetal framework itself instead.

## Remaining Frontend Work

- [x] Materialize the reviewed DXMT and Wine pins.
- [x] Stage exact-commit DXMT D3D12/DXGI PE and Unix components unchanged as a
  baseline from upstream CI while the local pinned build toolchain is prepared.
- [x] Package the standard application-facing DLL names under an ADX12-owned,
  hash-pinned runtime boundary without changing baseline semantics.
- [x] Route `D3D12CreateDevice`, DXGI factory creation, and WineMetal Unix
  calls through the isolated ADX12 runtime/host boundary.
- [x] Add COM identity, lifetime, parent, and interface-query probes.
- [x] Build the native D3D12/DXGI pair from the pinned source and ordered
  downstream patch queue with a hash-pinned Wine SDK.
- [ ] Run the same probes in fresh unmodified DXMT, D3DMetal, and vkd3d-proton
  processes.
- [x] Prove from module provenance and loaded-image guards that only the staged
  ADX12 runtime owns D3D12/DXGI/WineMetal in the ADX12 profile.

The Target 1 baseline exit condition is satisfied: a Windows probe loaded by
CrossOver creates an adapter/device/queue and tears them down without loading
another D3D renderer. The source-built native frontend and bounded
Mesa/KosmicKrisp compute integration now pass on that same boundary. Independent
comparison profiles and device-loss qualification remain.
