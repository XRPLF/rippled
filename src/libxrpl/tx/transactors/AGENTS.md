# AGENTS.md — transactors

See [tx/AGENTS.md](../AGENTS.md) for amendment-gating conventions that apply to all transactors, and the repo-level [AGENTS.md](../../../../AGENTS.md) for general guidance.

Pseudo-accounts (Vault, LoanBroker, AMM, ...) are exempt from `requireAuth` and freeze/deep-freeze checks as a class, not on a per-asset-type basis. Code that touches a pseudo-account (deposits, withdrawals, clawback, deletion, credential checks) must preserve that exemption rather than re-deriving it for each asset type.

Prefer a single object-level invariant over duplicating the same delta/balance check in every transactor that touches an object — e.g. one invariant asserting a Vault's pseudo-account balance and `assetsAvailable` always move together, rather than repeating that check in `VaultDeposit`, `VaultWithdraw`, `VaultClawback`, `LoanSet`, etc.

## Gating amendment-dependent code

Prefer a single amendment-enabled block and a single disabled block over scattering `rules.enabled(...)` checks through a function, even if the two blocks are similar.

When a file or function checks more than one amendment, name local enablement booleans per-amendment (e.g. `fix340Enabled` for `fixCleanup3_4_0`), not a generic `fixEnabled` — it becomes ambiguous once a second amendment is checked in the same scope.

Only use `UNREACHABLE` for genuinely impossible paths, not to avoid writing a test for one that's reachable but rare. When a branch marked `UNREACHABLE` is excluded from coverage, wrap it in `LCOV_EXCL_START`/`LCOV_EXCL_STOP`.
