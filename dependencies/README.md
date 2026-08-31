# ADX12 Dependencies

`upstreams.tsv` declares canonical repositories, branches, licenses,
integration modes, required paths, ownership, and submodule policy.
`upstreams.lock.tsv` pins the exact source revisions used by reproducible
builds. Recursive submodules remain pinned by their owning superproject
gitlinks; the updater initializes those exact commits and never follows a
submodule branch independently.

ADX12 modifications do not live as untracked edits in a dependency checkout:

- `patches/<component>/*.patch` contains ordered `git format-patch` changes;
- `overlays/<component>/` contains new ADX12-owned files that do not replace an
  upstream file accidentally;
- a patch or overlay must include its own license identifier;
- generated candidate locks and reports are written under `artifacts/` and are
  not release inputs until reviewed.

Integration modes in the manifest are policy labels:

- `patch-queue`: expected to receive narrowly scoped ADX12 changes;
- `semantic-donor`: audited for directly reusable backend-neutral code,
  separable D3D12 behavior, and application-facing regression tests; Vulkan
  storage and dispatch do not enter the Metal runtime;
- `reference`: materialized for tests and source comparison, not linked by
  default;
- `tool`: build/test tooling with no mandatory release-runtime dependency.

Apple D3DMetal is intentionally absent. It is an installed proprietary
comparison profile governed by `D3DMETAL_SEMANTIC_GAP_PLAN.md`, not a source
dependency that the updater may download, copy, or redistribute.
