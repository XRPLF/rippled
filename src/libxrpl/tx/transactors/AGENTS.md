# AGENTS.md — transactors

See [tx/AGENTS.md](../AGENTS.md) for amendment-gating conventions that apply to all transactors, and the repo-level [AGENTS.md](../../../../AGENTS.md) for general guidance.

Pseudo-accounts (Vault, LoanBroker, AMM, ...) are exempt from `requireAuth` and freeze/deep-freeze checks as a class, not on a per-asset-type basis. Code that touches a pseudo-account (deposits, withdrawals, clawback, deletion, credential checks) must preserve that exemption rather than re-deriving it for each asset type.

Prefer a single object-level invariant over duplicating the same delta/balance check in every transactor that touches an object — e.g. one invariant asserting a Vault's pseudo-account balance and `assetsAvailable` always move together, rather than repeating that check in `VaultDeposit`, `VaultWithdraw`, `VaultClawback`, `LoanSet`, etc.
