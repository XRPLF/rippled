# AGENTS.md — tx

See the repo-level [AGENTS.md](../../../AGENTS.md) for general build/test/style guidance.

## When an amendment is required

A change needs an amendment if it affects transaction processing, ledger objects, or anything else about the binary format or hash of the ledger. An amendment is optional if a change only affects what transactions get proposed for consensus (e.g. fee escalation). Otherwise, don't use one.

New amendments (and fixes, i.e. `fix*` amendments) are added to [`include/xrpl/protocol/detail/features.macro`](../../../include/xrpl/protocol/detail/features.macro), as an `XRPL_FEATURE(...)` or `XRPL_FIX(...)` entry added to the top of the list (the list is kept in reverse chronological order). Once the pre-amendment code path for a retired amendment is removed, move its entry to `XRPL_RETIRE_FEATURE(...)`/`XRPL_RETIRE_FIX(...)` instead of deleting it.

When adding a new amendment or transaction type, check its interaction with: invariants, fees, Deposit Auth, Batch transaction inclusion/exclusion, Permission Delegation inclusion/exclusion, Freeze/Deep Freeze (IOU) and Lock (MPT), Clawback, Credentials and Permissioned Domain, the case where the submitting account is the asset's issuer, and numeric over/underflow. Stick to existing paradigms rather than inventing new ones — consistency between features matters more than a locally "better" design.

New (or deleted) invariant checks must be amendment-gated: they introduce (or remove) a way for a transaction to fail, and an un-gated change risks validators disagreeing on a transaction's result, i.e. a network fork.

## Gating amendment-dependent code

Prefer a single amendment-enabled block and a single disabled block over scattering `rules.enabled(...)` checks through a function, even if the two blocks are similar.

When a file or function checks more than one amendment, name local enablement booleans per-amendment (e.g. `fix340Enabled` for `fixCleanup3_4_0`), not a generic `fixEnabled` — it becomes ambiguous once a second amendment is checked in the same scope.

Only use `UNREACHABLE` for genuinely impossible paths, not to avoid writing a test for one that's reachable but rare. When a branch marked `UNREACHABLE` is excluded from coverage, wrap it in `LCOV_EXCL_START`/`LCOV_EXCL_STOP`.
