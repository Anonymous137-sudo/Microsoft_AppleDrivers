# Microsoft_AppleDrivers

`Microsoft_AppleDrivers` is the workspace for ADX12, an independent,
work-in-progress Direct3D 12 userspace implementation for macOS on Apple
Silicon.

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
ADX12 semantic runtime
        |
        v
ADXIL / NIR compiler path
        |
        v
Native Metal backend
        |
        v
Metal / Apple GPU
```

## Project

- [`Direct3D_12(Feature_Level_12_2)`](Direct3D_12(Feature_Level_12_2)/README.md)
  contains the ADX12 architecture and will contain the implementation.
- The current repository state is **Phase 0: architecture and workspace
  bootstrap**. It does not yet provide a loadable `d3d12.dll`,
  `d3d12core.dll`, DXGI implementation, or hardware driver.
- Feature Level 12_2 is the target, not a current compatibility claim.

## Design Rules

- Keep the public D3D12/DXGI ABI, semantic runtime, compiler, and backend as
  separate layers.
- Preserve D3D12-visible behavior rather than reshaping it around Metal.
- Reuse mature open-source ABI, compiler, and reference machinery where its
  license and architecture permit; do not recreate established components for
  novelty.
- Keep backend-specific Metal objects below the ADX12 runtime boundary.
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

The project license has not yet been selected. Until a license is added, no
permission to copy, modify, or redistribute project-authored material is
granted beyond rights provided by applicable law. Third-party dependencies
will retain their own licenses and will be tracked separately before they are
introduced.
