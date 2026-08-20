# AGENTS.md

This file provides guidance to AI coding agents (Claude Code, and other AGENTS.md-compatible
tools) when working with code in this repository. `CLAUDE.md` is a symlink to this file.

For personal, untracked notes to an AI agent (not meant to be shared with other contributors),
use `AGENTS.local.md` / `CLAUDE.local.md` instead — see [CONTRIBUTING.md](./CONTRIBUTING.md).

## Build

Preferred: use the Nix devshell, which sets up the compiler, Conan, ccache, and (optionally) Rust automatically.

```bash
nix develop # default shell (clang on macOS, gcc on Linux); also .#gcc, .#clang, .#gcc-plain, .#clang-plain, .#apple-clang
```

Manual build (also what the devshell does under the hood):

```bash
./conan/init.sh         # one-time Conan profile/remote setup (auto-run inside nix develop)
mkdir .build && cd .build
conan install .. --output-folder . --build missing --settings build_type=Release
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -Dxrpld=ON -Dtests=ON ..
cmake --build . --parallel <N>
```

Key CMake options: `-Dxrpld=ON` (build the server binary, not just `libxrpl`), `-Dtests=ON`, `-Drust=ON` (builds `crates/`, requires cargo/rustc — off by default but always on in CI), `-Dunity=ON`, `-Dcoverage=ON`, `-Dwerr=ON`. `-Dverify_headers` is on by default; `cmake --build . --target verify-headers` compiles every header standalone.

Protocol codegen (from `.macro` files) must be regenerated and committed when changed:

```bash
cmake --build . --target setup_code_gen
cmake --build . --target code_gen
```

Rust crate tests (independent of the CMake build): `cargo test --manifest-path crates/Cargo.toml --workspace` (CI uses `cargo nextest`).

## Testing

Unit tests are a custom framework built into the `xrpld` binary itself (not Boost.Test/GTest/Catch):

```bash
./xrpld --unittest --unittest-jobs <N>     # run all suites; N = ~half of available cores
./xrpld --unittest SomeSuiteName           # run by name/prefix; an exact name match runs only that suite
```

(Multi-config generators produce the binary under e.g. `./Release/xrpld`.) Tests that run offline in under a minute should be automatic `--unittest` suites under `src/test/`; anything else is a manual/integration test. `tests/` (top-level, separate from `src/test/`) holds integration tests exercised against `libxrpl`/`xrpld`.

## Lint/Format

```bash
pip install pre-commit && pre-commit install
pre-commit run --all-files
pre-commit run clang-format --all-files # single hook
TIDY=1 pre-commit run clang-tidy        # clang-tidy is opt-in (needs local clang-tidy + generated headers)
```

Manual clang-tidy: build the `tidy_prerequisites` target first, then `run-clang-tidy -p build -allow-no-checks src tests` (add `-fix -format` to auto-fix).

## Architecture

- `include/xrpl/` + `src/libxrpl/` — the core protocol library: ledger, shamap, consensus, crypto, json, resource, nodestore, rdb, peerfinder, and `tx/` (transaction application: `Transactor.cpp`, `applySteps.cpp`, invariants, payment paths). `tx/transactors/` has one file per transaction type, grouped by subsystem: `escrow/`, `vault/`, `lending/`, `sponsor/`, `nft/`, `token/` (MPT), `payment_channel/`, `permissioned_domain/`, `dex/`, `oracle/`, `did/`, `credentials/`, `bridge/`, `check/`, `delegate/`, `account/`, `system/`. Any change to transaction-processing behavior must be gated behind an Amendment.
- `src/xrpld/` — the server application built on top of `libxrpl`: `app`, `core`, `overlay` (P2P networking), `peerfinder`, `perflog`, `rpc`, `shamap`. `main` builds an `ApplicationImp` implementing `Application`; most components hold a reference to it (`app_`), giving broad cross-component access — expect to trace call chains through `Application&`.
- `src/test/` — unit tests mirroring the subsystems above, plus `jtx/` (the transaction-building test DSL — e.g. `jtx/escrow.h`, `jtx/vault.h`, `jtx/sponsor.h`, `jtx/permissioned_dex.h`) and `unit_test/` (the custom test framework itself, derived from Beast).
- `src/tests/` — a second, separate tree of integration-style tests for `libxrpl`.
- `crates/` — a Rust workspace (only built with `-Dxrpld -Drust=ON`) bridged into C++ via `cxxbridge`/the `cxx` crate; currently just a `hello_world` interop scaffold. Requires the Rust toolchain pinned in `rust-toolchain.toml` (the Nix devshell provides it automatically).

## Code Style (see `docs/CodingStyle.md` and `CONTRIBUTING.md` for full detail)

- New file placement is strict: `libxrpl` headers → `include/xrpl`; `libxrpl` sources → `src/libxrpl`; other non-test server code → `src/xrpld`; tests → `src/test`; benchmarks → `src/benchmarks`.
- Header includes must stay levelized (checked by `.github/scripts/levelization`).
- Allman braces, tabs-as-4-spaces (no literal tabs), 80-char lines, east `const`, no naked `new`/`delete`, `*`/`&` bound to the type not the variable (`SomeObject* myObject`), never declare multiple pointers/refs in one statement.
- Class member order: private members first, then the six special members in order (dtor, default ctor, copy ctor, copy assign, move ctor, move assign).
- Use `XRPL_ASSERT`/`UNREACHABLE` instead of raw `assert`/`assert(false)` outside constexpr functions and unit tests; each needs a unique name of the form `scope::function : short description` (used for Antithesis instrumentation).
- Commits: imperative subject line ≤50 chars (72 hard limit), capitalized, no trailing period; each commit should build and pass tests on its own; prefer squashing to one logical commit per PR.
