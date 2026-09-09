# Architecture

Paths below reflect the current layout; update this doc if modularization moves a subsystem to a different directory.

- `include/xrpl/` + `src/libxrpl/` — the core protocol library: ledger, shamap, consensus, crypto, json, resource, nodestore, rdb, peerfinder, and `tx/` (transaction application: `Transactor.cpp`, `applySteps.cpp`, invariants, payment paths — see [src/libxrpl/tx/AGENTS.md](./src/libxrpl/tx/AGENTS.md) for amendment-gating conventions). `tx/transactors/` has one file per transaction type, grouped by subsystem: `escrow/`, `vault/`, `lending/`, `sponsor/`, `nft/`, `token/` (MPT), `payment_channel/`, `permissioned_domain/`, `dex/`, `oracle/`, `did/`, `credentials/`, `bridge/`, `check/`, `delegate/`, `account/`, `system/`.
- `src/xrpld/` — the server application built on top of `libxrpl`: `app`, `core`, `overlay` (P2P networking), `peerfinder`, `perflog`, `rpc`, `shamap`. `main` builds an `ApplicationImp` implementing `Application`; most components hold a reference to it (`app_`), giving broad cross-component access — expect to trace call chains through `Application&`.
- `src/test/` — unit tests mirroring the subsystems above, plus `jtx/` (the transaction-building test DSL — e.g. `jtx/escrow.h`, `jtx/vault.h`, `jtx/sponsor.h`, `jtx/permissioned_dex.h`) and `unit_test/` (the custom test framework itself, derived from Beast).
- `src/tests/` — unit tests for `libxrpl` written in `gtest`, gradually replacing the `src/test` equivalents.
- `crates/` — a Rust workspace (only built with `-Dxrpld -Drust=ON`) bridged into C++ via `cxxbridge`/the `cxx` crate; currently just a `hello_world` interop scaffold. Requires the Rust toolchain pinned in `rust-toolchain.toml` (the Nix devshell provides it automatically).
