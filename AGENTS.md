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
- New tests should be written using `gtest` under `src/tests/` unless that isn't possible, in which case fall back to the legacy Beast framework under `src/test/` (see [src/test/AGENTS.md](./src/test/AGENTS.md) for conventions specific to that directory). `tests/` (top-level) holds integration tests exercised against `libxrpl`/`xrpld`.

## Lint/Format

See [CONTRIBUTING.md](./CONTRIBUTING.md#pre-commit-hooks) for `pre-commit` setup and [CONTRIBUTING.md](./CONTRIBUTING.md#clang-tidy) for `clang-tidy` (opt-in, needs local `clang-tidy` and generated headers).

## Code Style

New file placement and header levelization: see [CONTRIBUTING.md](./CONTRIBUTING.md#before-making-a-pull-request). Braces, whitespace, member order, and other conventions: see [docs/CodingStyle.md](./docs/CodingStyle.md). `XRPL_ASSERT`/`UNREACHABLE` contracts: see [CONTRIBUTING.md](./CONTRIBUTING.md#contracts-and-instrumentation). Commit messages: see [CONTRIBUTING.md](./CONTRIBUTING.md#good-commit-messages). New public functions/methods need a Doxygen-style comment.

Comments should explain _why_, not _what_/_how_ — the code already shows that. Only describe what/how when the code itself would otherwise be confusing (a non-obvious workaround, a subtle invariant, a surprising constraint). Write new or changed comments as full sentences: start with a capital letter and end with a period.

## Code review

When reviewing a change, or self-reviewing your own before asking for review, apply [docs/CodeReview.md](./docs/CodeReview.md): correctness rules first, then style and convention rules. Cite the rule number (`CR-N`) in each review comment. Never skip, disable or weaken tests, or remove an amendment gate, to satisfy a review comment; raise the conflict instead.

### Review perspectives

Examine every change from each of these angles, individually and as a whole:

1. **Safety.** Log at the right level with the right amount of detail. Use `Throw`, `logicError`, `XRPL_ASSERT`, `SOMETIMES`, `REACHABLE` and `UNREACHABLE` correctly (see [CONTRIBUTING.md](./CONTRIBUTING.md#contracts-and-instrumentation)). No crash may be reachable from data supplied by a peer or client over RPC, P2P or websocket.
2. **Correctness.** The code does what its name, comments and commit message say. It protects against malicious peers and clients without penalizing honest peers or regular consensus, transaction and node processing. Variables, constants and functions use `constexpr`, `consteval`, `explicit`, `inline` and other C++23 facilities appropriately.
3. **Performance.** `std::move` where appropriate, no unnecessary copies, no locking unless absolutely necessary. Minimize memory and CPU use; maximize transaction throughput.
4. **Conciseness.** The code is easy to read, with no duplication and helper functions where they pay off. Touched functions have docstrings. Comments and commit messages describe what the code does, not what it used to do or could do; condense long comments. The change passes `clang-format` and `clang-tidy` (see [CONTRIBUTING.md](./CONTRIBUTING.md#pre-commit-hooks)).
5. **Scope.** Each pull request is self-contained and fixes one problem. Split an oversized change into dependent pull requests; combine small related ones. A later pull request does not rework code changed by an earlier one unless necessary.
6. **Coverage.** Test coverage of touched code is at least 90% (see [BUILD.md](./BUILD.md#coverage-report)). `LCOV_EXCL` stanzas exclude only untestable or unreachable blocks.

## Architecture

See [ARCHITECTURE.md](./ARCHITECTURE.md) for the directory-by-directory map of the codebase.

## Keeping docs current

When you add or change a convention, or touch a subsystem that has its own `AGENTS.md`, `README.md`, or `ARCHITECTURE.md`, update that documentation in the same change rather than leaving it stale.
