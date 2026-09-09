# AGENTS.md

This file provides guidance to AI coding agents (Claude Code, and other AGENTS.md-compatible tools) when working with code in this repository.

## Build

Recommended on Linux/macOS: the Nix devshell sets up the compiler, Conan, ccache, and (optionally) Rust automatically.

```bash
nix develop
```

Not required — contributors can use their own toolchain/build flow instead. For alternate devshell variants (specific compiler, no-compiler, coverage), see [docs/build/nix.md](./docs/build/nix.md). For manual (non-Nix) build steps, CMake options, and protocol codegen commands, see [BUILD.md](./BUILD.md) (`## Steps`, `## Options`, `## Code generation`).

Rust crate tests (independent of the CMake build): `cargo test --manifest-path crates/Cargo.toml --workspace` (CI uses `cargo nextest`).

## Testing

Unit tests are a custom framework built into the `xrpld` binary itself (not Boost.Test/GTest/Catch); see [CONTRIBUTING.md](./CONTRIBUTING.md#unit-tests) for the basic invocation. Notes not covered there:

- A suite's `--unittest` name is built from the arguments to its `BEAST_DEFINE_TESTSUITE`/`BEAST_DEFINE_TESTSUITE_PRIO` macro (usually at the bottom of the test file), in reverse order and joined with `.`: `BEAST_DEFINE_TESTSUITE(Credentials, app, xrpl)` → `xrpl.app.Credentials`.
- `--unittest-arg` does nothing — don't use it.
- Tests that run offline in under a minute should be automatic `--unittest` suites; anything else is a manual/integration test.
- New tests should be written using `gtest` under `src/tests/` unless that isn't possible, in which case fall back to the legacy Beast framework under `src/test/` (see [src/test/AGENTS.md](./src/test/AGENTS.md) for conventions specific to that directory). `tests/` (top-level) holds integration tests exercised against `libxrpl`/`xrpld`.

## Lint/Format

See [CONTRIBUTING.md](./CONTRIBUTING.md#pre-commit-hooks) for `pre-commit` setup and [CONTRIBUTING.md](./CONTRIBUTING.md#clang-tidy) for `clang-tidy` (opt-in, needs local `clang-tidy` and generated headers).

## Code Style

New file placement and header levelization: see [CONTRIBUTING.md](./CONTRIBUTING.md#before-making-a-pull-request). Braces, whitespace, member order, and other conventions: see [docs/CodingStyle.md](./docs/CodingStyle.md). `XRPL_ASSERT`/`UNREACHABLE` contracts: see [CONTRIBUTING.md](./CONTRIBUTING.md#contracts-and-instrumentation). Commit messages: see [CONTRIBUTING.md](./CONTRIBUTING.md#good-commit-messages). New public functions/methods need a Doxygen-style comment.

Comments should explain _why_, not _what_/_how_ — the code already shows that. Only describe what/how when the code itself would otherwise be confusing (a non-obvious workaround, a subtle invariant, a surprising constraint).

## Architecture

See [ARCHITECTURE.md](./ARCHITECTURE.md) for the directory-by-directory map of the codebase.

## Keeping docs current

When you add or change a convention, or touch a subsystem that has its own `AGENTS.md`, `README.md`, or `ARCHITECTURE.md`, update that documentation in the same change rather than leaving it stale.
