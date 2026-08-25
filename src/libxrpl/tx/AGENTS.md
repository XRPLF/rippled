# AGENTS.md — tx

See the repo-level [AGENTS.md](../../../AGENTS.md) for general build/test/style guidance.

Any change to transaction-processing behavior must be gated behind an amendment. New amendments (and fixes, i.e. `fix*` amendments) are added to [`include/xrpl/protocol/detail/features.macro`](../../../include/xrpl/protocol/detail/features.macro), as an `XRPL_FEATURE(...)` or `XRPL_FIX(...)` entry added to the top of the list (the list is kept in reverse chronological order). Once the pre-amendment code path for a retired amendment is removed, move its entry to `XRPL_RETIRE_FEATURE(...)`/`XRPL_RETIRE_FIX(...)` instead of deleting it.
