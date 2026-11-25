# AGENTS.md — transactors

Prefer a single object-level invariant over duplicating the same delta/balance check in every transactor that touches an object — e.g. one invariant asserting a Vault's pseudo-account balance and `assetsAvailable` always move together, rather than repeating that check in `VaultDeposit`, `VaultWithdraw`, `VaultClawback`, `LoanSet`, etc.

## Gating amendment-dependent code

Prefer a single amendment-enabled block and a single disabled block over scattering `rules.enabled(...)` checks through a function, even if the two blocks are similar.

When a file or function checks more than one amendment, name local enablement booleans per-amendment (e.g. `fix340Enabled` for `fixCleanup3_4_0`), not a generic `fixEnabled` — it becomes ambiguous once a second amendment is checked in the same scope.
