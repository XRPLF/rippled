# AGENTS.md — tx

See the repo-level [AGENTS.md](../../../AGENTS.md) for general build/test/style guidance.

Any change to transaction-processing behavior must be gated behind an amendment. New amendments (and fixes, i.e. `fix*` amendments) are added to [`include/xrpl/protocol/detail/features.macro`](../../../include/xrpl/protocol/detail/features.macro), as an `XRPL_FEATURE(...)` or `XRPL_FIX(...)` entry added to the top of the list (the list is kept in reverse chronological order). Once the pre-amendment code path for a retired amendment is removed, move its entry to `XRPL_RETIRE_FEATURE(...)`/`XRPL_RETIRE_FIX(...)` instead of deleting it.

## Preflight helpers

Prefer the helpers in [`include/xrpl/tx/helpers/PreflightHelpers.h`](../../../include/xrpl/tx/helpers/PreflightHelpers.h) over ad-hoc checks in transactor preflight code:

- `checkBounds(value, min, max)` — inclusive range check.
- `checkSize(container, max)` — container size is at most `max`.
- `checkSizeAndNonEmpty(container, max)` — container is non-empty and size is at most `max`.
- `isZeroId(id)` — hash-like ID (e.g. a `uint256` object ID) is unset (compares against `beast::kZero`).
- `isPositiveXRPAmount(amount)` — amount is XRP and strictly positive.
- `isPositiveAmount(amount)` — amount of any asset is strictly positive.
- `isBadCurrency(currency)` — currency equals the reserved `badCurrency()` sentinel.

Use them to keep preflight logic uniform and to make the intent of each check explicit. If the same pattern appears in more than one transactor, add a new helper here rather than inlining the check.
