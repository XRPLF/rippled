# AGENTS.md

This file provides guidance to AI coding agents (Claude Code, and other AGENTS.md-compatible tools) when working with code in this repository.

## Build

For the build steps, CMake options, and protocol codegen commands, see [BUILD.md](./BUILD.md) (`## Steps`, `## Options`, `## Code generation`). Nix development shells are available in the repo (not required) — see [docs/build/nix.md](./docs/build/nix.md) for setup and variants.

Rust crate tests (independent of the CMake build): `cargo test --manifest-path crates/Cargo.toml --workspace` (CI uses `cargo nextest`).

## Testing

The legacy unit tests under `src/test/` are a custom framework (derived from Beast, not Boost.Test/GTest/Catch) built into the `xrpld` binary; see [CONTRIBUTING.md](./CONTRIBUTING.md#unit-tests) for the basic invocation. The gtest suites under `src/tests/` build into the separate `xrpl_tests` and `xrpld_tests` binaries (run with `--gtest_filter=...`). Notes not covered there:

- A suite's `--unittest` name is built from the arguments to its `BEAST_DEFINE_TESTSUITE`/`BEAST_DEFINE_TESTSUITE_PRIO` macro (usually at the bottom of the test file), in reverse order and joined with `.`: `BEAST_DEFINE_TESTSUITE(Credentials, app, xrpl)` → `xrpl.app.Credentials`.
- `--unittest-arg=<string>` passes one string to every suite that runs; a suite reads it with `arg()` and interprets it itself. Only a few suites use it (e.g. `TheoreticalQuality`, the nodestore `import` test); for the rest it has no effect.
- Tests that run offline in under a minute should be automatic `--unittest` suites; anything else is a manual/integration test.
- Before you add a new test, search `src/test/` and `src/tests/` for a suite that already covers the same feature or code path. If one exists, add the test there, even if that suite uses the legacy Beast framework under `src/test/` (see [src/test/AGENTS.md](./src/test/AGENTS.md) for conventions specific to that directory). Otherwise, write it using `gtest` under `src/tests/`, and fall back to `src/test/` only if `gtest` isn't possible. `tests/` (top-level) holds integration tests exercised against `libxrpl`/`xrpld`.

## Lint/Format

See [CONTRIBUTING.md](./CONTRIBUTING.md#pre-commit-hooks) for `pre-commit` setup and [docs/cpp/clang-tidy.md](./docs/cpp/clang-tidy.md) for `clang-tidy` (opt-in, needs local `clang-tidy` and generated headers).

## Code Style

C++ conventions: see [docs/cpp/guidelines.md](./docs/cpp/guidelines.md).
Braces, whitespace, and member order: see [docs/CodingStyle.md](./docs/CodingStyle.md).
`XRPL_ASSERT`/`UNREACHABLE` contracts: see [docs/cpp/antithesis.md](./docs/cpp/antithesis.md).
Commit messages: see [CONTRIBUTING.md](./CONTRIBUTING.md#good-commit-messages).

Comment rules: see [docs/cpp/guidelines.md](./docs/cpp/guidelines.md#comments); they apply to comments in any language.

## Architecture

See [ARCHITECTURE.md](./ARCHITECTURE.md) for the directory-by-directory map of the codebase.

## Keeping docs current

When you add or change a convention, or touch a subsystem that has its own `AGENTS.md`, `README.md`, or `ARCHITECTURE.md`, update that documentation in the same change rather than leaving it stale.
