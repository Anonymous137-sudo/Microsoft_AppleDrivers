# ADX12

ADX12 is the planned Direct3D 12 userspace driver/runtime in the
`Microsoft_AppleDrivers` workspace. It targets macOS on Apple Silicon and
Direct3D Feature Level 12_2.

## Status

`Phase 0 - architecture and repository bootstrap`

No Direct3D entry points, COM objects, shader compiler, command submission,
DXGI presentation path, or feature-level implementation are claimed in this
commit. The feature-level name records the engineering target only.

## Active Architecture

```text
D3D12 application under Wine / CrossOver
        |
        v
D3D12 + DXGI ABI frontend
        |
        v
ADX12-owned semantic runtime
        |
        +---- resources, heaps, descriptors, root signatures
        +---- command lists, queues, barriers, fences, queries
        +---- pipeline identity, caches, capability reporting
        |
        v
DXIL -> bootstrap compiler path -> Mesa NIR -> MSL
        |
        v
ADX12 native Metal backend
        |
        v
Metal / AGX
```

The runtime will own Direct3D semantics. Metal will remain an execution
backend rather than becoming the public object model. A separate
AVK143-backed path may be used for differential validation, but it is not the
native runtime path.

## Architecture Specification

[`ADX12_Extensive_Project_Architecture_and_BringUp.pdf`](ADX12_Extensive_Project_Architecture_and_BringUp.pdf)
is the authoritative project genesis, architecture, dependency policy,
bring-up roadmap, validation strategy, and initial repository plan.

## Next Engineering Gate

The next instructed pass should turn the architecture into the initial source
tree and select the exact dependency and licensing boundaries before runtime
implementation begins. The first executable milestone remains a loadable
ADX12 library and deterministic bootstrap test, followed by the D3D12/COM
device boundary.
