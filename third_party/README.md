# Pinned External Source

This directory exposes ADX12's external source dependencies as ordinary,
GitHub-browsable gitlinks at the exact revisions accepted in
`dependencies/upstreams.lock.tsv`. They are not opaque release blobs and are
not silently fetched from moving branches during a reproducible build.

Initialize every dependency locally with:

```sh
git submodule update --init --recursive
```

The assembled ADX12 D3D12/DXGI/DXMT/WineMetal implementation is intentionally
not a baseline-only submodule. Its complete downstream source is committed at
`source/dxmt-adx12/`, while its ordered reviewable changes remain in
`dependencies/patches/dxmt/`.

Each dependency retains its own license. A gitlink makes source and revision
visible; it does not make every component a linked runtime dependency. The
integration role and required paths remain defined in
`dependencies/upstreams.tsv`.
