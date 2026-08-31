# ADX12 Upstream Synchronization

## Purpose

ADX12 reuses mature upstream machinery but must never lose downstream work when
those projects advance. The updater treats upstream source, downstream changes,
and accepted build pins as three separate objects:

```text
canonical upstream branch
        +
committed ADX12 patch queue / overlay
        +
reviewed immutable lock
        =
reproducible materialized dependency
```

The updater does not edit a developer checkout, rebase a downstream branch,
force-push a fork, or silently replace a lock.

## Files

- `dependencies/upstreams.tsv`: repository, branch, license, integration mode,
  required source paths, role, and submodule policy;
- `dependencies/upstreams.lock.tsv`: accepted immutable commit for each source;
- `dependencies/patches/<component>/*.patch`: ordered downstream commits;
- `dependencies/overlays/<component>/`: new ADX12-owned files;
- `artifacts/upstream-sync/`: generated status, candidate locks, and reports.

Required-path checks prevent a successful Git merge from hiding an upstream
architecture removal such as DXMT D3D12/WineMetal or Mesa NIR/KosmicKrisp.
Recursive submodules are initialized at the exact gitlinks selected by each
locked superproject; they are never advanced independently behind upstream's
back.

## Commands

Check all upstream heads without cloning or changing pins:

```sh
./scripts/adx12-upstream-sync.sh check all
```

Prepare a reviewable candidate lock and prove that patch queues still apply:

```sh
./scripts/adx12-upstream-sync.sh prepare all
```

Materialize exactly one accepted dependency for a build:

```sh
./scripts/adx12-upstream-sync.sh materialize dxmt .adx12-deps/dxmt
```

Verify all accepted pins and downstream deltas:

```sh
./scripts/adx12-upstream-sync.sh verify all
```

The fixture test proves the preservation invariant with an upstream commit and
a downstream patch touching the same file in compatible locations:

```sh
./scripts/test-upstream-sync.sh
```

## Promotion

The weekly GitHub workflow prepares a candidate lock, verifies every selected
dependency, runs the ADX12 frontend smoke suite, and opens a pull request. It
does not merge the pull request. A candidate is promoted only after:

1. every previous pin is an ancestor of the new upstream pin;
2. every patch and overlay applies without conflict;
3. required source paths remain present;
4. `git diff --check` passes for the downstream delta;
5. component builds and regression tests pass;
6. license and API changes are reviewed.

If upstream rewrites history, removes a required subsystem, or conflicts with a
downstream patch, the update fails closed with the accepted lock unchanged.

## Dependency Boundaries

Reference components may be materialized for tests but do not become release
runtime dependencies automatically. D3DMetal is never handled by this updater;
it is an installed comparison runtime and is governed by the separate semantic
gap plan.
