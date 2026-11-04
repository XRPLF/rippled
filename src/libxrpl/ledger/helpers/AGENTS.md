# AGENTS.md — ledger/helpers

A helper that takes an `SLE`/`std::shared_ptr<SLE const>` should `XRPL_ASSERT` that it's non-null and of the expected ledger-entry type at entry, and keep a real runtime check/error-return alongside the assert (asserts compile out in release builds) — the established idiom in this directory is `std::expected<..., TER>`, returning `std::unexpected(tec*)` on failure. Don't invent a new error-handling idiom for this.
