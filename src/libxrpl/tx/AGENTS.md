# AGENTS.md — tx

## When an amendment is required

A change needs an amendment if it affects transaction processing, ledger objects, or anything else about the binary format or hash of the ledger. An amendment is optional if a change only affects what transactions get proposed for consensus (e.g. fee escalation). Otherwise, don't use one.

New amendments (and fixes, i.e. `fix*` amendments) are added to [`include/xrpl/protocol/detail/features.macro`](../../../include/xrpl/protocol/detail/features.macro), as an `XRPL_FEATURE(...)` or `XRPL_FIX(...)` entry added to the top of the list (the list is kept in reverse chronological order). Once the pre-amendment code path for a retired amendment is removed, move its entry to `XRPL_RETIRE_FEATURE(...)`/`XRPL_RETIRE_FIX(...)` instead of deleting it.

When adding a new amendment or transaction type, check its interaction with: invariants, fees, Deposit Auth, Batch transaction inclusion/exclusion, Permission Delegation inclusion/exclusion, Freeze/Deep Freeze (IOU) and Lock (MPT), Clawback, Credentials and Permissioned Domain, the case where the submitting account is the asset's issuer, and numeric over/underflow. Stick to existing paradigms rather than inventing new ones — consistency between features matters more than a locally "better" design.

New (or deleted) invariant checks must be amendment-gated: they introduce (or remove) a way for a transaction to fail, and an un-gated change risks validators disagreeing on a transaction's result, i.e. a network fork.

See [transactors/AGENTS.md](./transactors/AGENTS.md) for conventions on writing the amendment-gated code itself.

A change to transaction/signing behavior that's visible through the public API also needs an `API-CHANGELOG.md` entry — see [../../xrpld/rpc/AGENTS.md](../../xrpld/rpc/AGENTS.md) for the full rule.
