# AGENTS.md

This file provides guidance to AI coding agents (Claude Code, and other AGENTS.md-compatible tools) when working with code in this repository.

## Build

Required on Linux/macOS: use the Nix devshell, which sets up the compiler, Conan, ccache, and (optionally) Rust automatically.

```bash
nix develop
```

For alternate devshell variants (specific compiler, no-compiler, coverage), see [docs/build/nix.md](./docs/build/nix.md). For the manual build steps, CMake options, and protocol codegen commands, see [BUILD.md](./BUILD.md) (`## Steps`, `## Options`, `## Code generation`).

Rust crate tests (independent of the CMake build): `cargo test --manifest-path crates/Cargo.toml --workspace` (CI uses `cargo nextest`).

## Testing

Unit tests are a custom framework built into the `xrpld` binary itself (not Boost.Test/GTest/Catch); see [CONTRIBUTING.md](./CONTRIBUTING.md#unit-tests) for the basic invocation. Notes not covered there:

- A suite's `--unittest` name is built from the arguments to its `BEAST_DEFINE_TESTSUITE`/`BEAST_DEFINE_TESTSUITE_PRIO` macro (usually at the bottom of the test file), in reverse order and joined with `.`: `BEAST_DEFINE_TESTSUITE(Credentials, app, xrpl)` → `xrpl.app.Credentials`.
- `--unittest-arg` does nothing — don't use it.
- Tests that run offline in under a minute should be automatic `--unittest` suites; anything else is a manual/integration test.
- New tests should be written using `gtest` under `src/tests/` unless that isn't possible, in which case fall back to the legacy Beast framework under `src/test/`. `tests/` (top-level) holds integration tests exercised against `libxrpl`/`xrpld`.

## Lint/Format

See [CONTRIBUTING.md](./CONTRIBUTING.md#pre-commit-hooks) for `pre-commit` setup and [CONTRIBUTING.md](./CONTRIBUTING.md#clang-tidy) for `clang-tidy` (opt-in, needs local `clang-tidy` and generated headers).

## Code Style

New file placement and header levelization: see [CONTRIBUTING.md](./CONTRIBUTING.md#before-making-a-pull-request). Braces, whitespace, member order, and other conventions: see [docs/CodingStyle.md](./docs/CodingStyle.md). `XRPL_ASSERT`/`UNREACHABLE` contracts: see [CONTRIBUTING.md](./CONTRIBUTING.md#contracts-and-instrumentation). Commit messages: see [CONTRIBUTING.md](./CONTRIBUTING.md#good-commit-messages).

## Architecture

Paths below reflect the current layout; update this section if modularization moves a subsystem to a different directory.

- `include/xrpl/` + `src/libxrpl/` — the core protocol library: ledger, shamap, consensus, crypto, json, resource, nodestore, rdb, peerfinder, and `tx/` (transaction application: `Transactor.cpp`, `applySteps.cpp`, invariants, payment paths). `tx/transactors/` has one file per transaction type, grouped by subsystem: `escrow/`, `vault/`, `lending/`, `sponsor/`, `nft/`, `token/` (MPT), `payment_channel/`, `permissioned_domain/`, `dex/`, `oracle/`, `did/`, `credentials/`, `bridge/`, `check/`, `delegate/`, `account/`, `system/`. Any change to transaction-processing behavior must be gated behind an Amendment.
- `src/xrpld/` — the server application built on top of `libxrpl`: `app`, `core`, `overlay` (P2P networking), `peerfinder`, `perflog`, `rpc`, `shamap`. `main` builds an `ApplicationImp` implementing `Application`; most components hold a reference to it (`app_`), giving broad cross-component access — expect to trace call chains through `Application&`.
- `src/test/` — unit tests mirroring the subsystems above, plus `jtx/` (the transaction-building test DSL — e.g. `jtx/escrow.h`, `jtx/vault.h`, `jtx/sponsor.h`, `jtx/permissioned_dex.h`) and `unit_test/` (the custom test framework itself, derived from Beast).
- `src/tests/` — a second, separate tree of integration-style tests for `libxrpl`.
- `crates/` — a Rust workspace (only built with `-Dxrpld -Drust=ON`) bridged into C++ via `cxxbridge`/the `cxx` crate; currently just a `hello_world` interop scaffold. Requires the Rust toolchain pinned in `rust-toolchain.toml` (the Nix devshell provides it automatically).
