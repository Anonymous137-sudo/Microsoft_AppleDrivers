# ADX12 Target 1 Validation

## Baseline

- DXMT source revision: `e55ad281c60be97f8815b5848e57cfd9f967d759`
- Host class: CrossOver 26.3 on Apple Silicon macOS
- Host policy: Windows process/loader environment only
- Loader policy: `d3d12,dxgi=n;winemetal=b`
- Renderer policy: `adx12-only`

ADX12 builds D3D12 and DXGI as stripped native PEs. WineMetal remains a builtin
PE so its thunk reaches the paired Unix module. ADX12 verifies the source pin,
downstream patchset, runtime ABI, loader classes, runtime paths, and SHA-256
hashes before launch.

## Passing Contract

- [x] Exact WineMetal PE/Unix bridge admission
- [x] Exact DXGI and D3D12 PE admission
- [x] DXGI factory creation and `IDXGIFactory6` query
- [x] Stable factory `IUnknown` identity
- [x] Adapter enumeration, description, `IDXGIAdapter4`, and parent query
- [x] D3D12 device creation and `ID3D12Device1` query
- [x] Raw baseline feature-level query recorded without lifting ADX12 gates
- [x] Direct command queue creation and queue-to-device query
- [x] Ordered COM teardown
- [x] Trace rejection for WineD3D, WineVulkan, DXVK, and vkd3d renderers
- [x] Byte-for-byte reproducible frontend probe and package checksum manifest
- [x] Source-built native D3D12/DXGI pair with zero PE timestamps and no
  developer path metadata
- [x] Versioned `ADX12GetCompilerABI` rejects host or stale D3D12 modules

The strict smoke command is `scripts/test-crossover-frontend.sh`. It reports
the relevant loader trace on failure and refuses an unmarked or hash-mismatched
CrossOver host overlay.

## Remaining Qualification

- [ ] Run the same object/lifetime probes in independent DXMT, D3DMetal, and
  vkd3d-proton comparison processes.
- [ ] Add D3D12 debug-message and device-loss lifecycle coverage.

These qualification tasks do not change the Target 1 renderer boundary. No
CrossOver/Wine renderer is an ADX12 fallback.
