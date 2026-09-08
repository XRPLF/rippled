# AGENTS.md — ledger/helpers

See the repo-level [AGENTS.md](../../../../AGENTS.md) for general guidance.

A helper that takes an `SLE`/`std::shared_ptr<SLE const>` should `XRPL_ASSERT` that it's non-null and of the expected ledger-entry type at entry, and keep a real runtime check/error-return alongside the assert (asserts compile out in release builds). Don't invent a new error-handling idiom for this (e.g. `std::unexpected`) — return the existing `tec`/`ter`/`tef` code used elsewhere in the codebase.
