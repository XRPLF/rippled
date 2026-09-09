# AGENTS.md — ledger/helpers

See the repo-level [AGENTS.md](../../../../AGENTS.md) for general guidance.

A helper that takes an `SLE`/`std::shared_ptr<SLE const>` should `XRPL_ASSERT` that it's non-null and of the expected ledger-entry type at entry, and keep a real runtime check/error-return alongside the assert (asserts compile out in release builds) — the established idiom in this directory is `std::expected<..., TER>`, returning `std::unexpected(tec*)` on failure. Don't invent a new error-handling idiom for this.

Prefer a single amendment-enabled block and a single disabled block over scattering `rules.enabled(...)` checks through a function, even if the two blocks are similar. When a file or function checks more than one amendment, name local enablement booleans per-amendment (e.g. `fix340Enabled` for `fixCleanup3_4_0`), not a generic `fixEnabled`.

Only use `UNREACHABLE` for genuinely impossible paths, not to avoid writing a test for one that's reachable but rare. When a branch marked `UNREACHABLE` is excluded from coverage, wrap it in `LCOV_EXCL_START`/`LCOV_EXCL_STOP`.
