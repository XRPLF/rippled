# `CanDelete.cpp` — `can_delete` RPC Handler

## Role in the System

`CanDelete.cpp` implements `doCanDelete`, the admin RPC handler for the `can_delete` command. It exists to let node operators manually control XRPL's online-deletion feature when running in **advisory delete** mode. In that mode, the `SHAMapStore` background thread will only prune historical ledger data up to an explicitly approved sequence number — the node won't automatically discard ledgers based solely on its configured deletion depth. This file is the control surface that reads or writes that threshold.

## Advisory Delete Guard

The first thing `doCanDelete` does is check `context.app.getSHAMapStore().advisoryDelete()`. If the node was not configured with `advisory_delete=1` in its `[node_db]` config section, the handler immediately returns `rpcNOT_ENABLED`. This is a hard gate: the `can_delete` command is meaningless on a node running automatic deletion, and exposing it there would give operators a false sense of control. The flag is read from `SHAMapStoreImp::advisoryDelete_`, set at startup from the config file.

## Read vs. Write Dispatch

The function is both a getter and a setter, decided by the presence of the `can_delete` key in the request parameters. If the parameter is absent, the handler calls `getSHAMapStore().getCanDelete()` and returns the current threshold. If the parameter is present, it parses the value, calls `setCanDelete(canDeleteSeq)`, and returns the newly set value. Reflecting the set value back (rather than just returning success) allows callers to verify the actual resulting state with a single round-trip.

## Multi-Format Input Parsing

The parameter accepts several representations of a ledger sequence, processed in a deliberate priority order:

1. **Native JSON unsigned integer** (`canDelete.isUInt()`): used directly as the sequence number. This fast path avoids string allocation entirely.

2. **All-digit string**: detected with `find_first_not_of("0123456789")` before any other string comparison. The digits-only check comes first so that a string like `"12345"` isn't accidentally caught by the keyword matching that follows. Conversion uses `beast::lexicalCast<std::uint32_t>`, which throws on overflow rather than silently truncating.

3. **Keyword `"never"`**: maps to sequence `0`. Setting the threshold to zero effectively tells the store it may not delete any ledger — pausing pruning without disabling the feature.

4. **Keyword `"always"`**: maps to `std::numeric_limits<std::uint32_t>::max()`. This is a deliberate sentinel meaning "delete everything eligible," authorizing the store to prune as aggressively as its configuration allows.

5. **Keyword `"now"`**: resolves to `getSHAMapStore().getLastRotated()`, which is the sequence of the most recently completed rotation point. If `getLastRotated()` returns zero — meaning no rotation has occurred yet since startup — the handler returns `rpcNOT_READY` rather than setting the threshold to an invalid value.

6. **256-bit hex ledger hash**: `uint256::parseHex` is tried when none of the above match. If it parses, the handler does a full ledger lookup via `context.ledgerMaster.getLedgerByHash()`. A missing ledger returns `rpcLGR_NOT_FOUND`. A found ledger yields `ledger->header().seq`, converting the user-supplied hash into the sequence number that `SHAMapStore` actually operates on.

7. **Everything else**: falls to `rpcINVALID_PARAMS`.

All string comparisons are performed after `boost::to_lower`, making the keywords case-insensitive. This normalization happens before the keyword and hex branches, so `"NEVER"`, `"Always"`, and `"NOW"` are all valid inputs.

## Design Observations

The ordering of the string parsing branches is significant. Checking for all-digits before keywords prevents a numeric string from being mistaken for a keyword. Placing the hex check last (before the error fallback) is intentional: a 64-character hex string will never be all-digits, never match a keyword, and `parseHex` consumes the string cleanly or fails, making the final `else` a true catch-all.

The `now` keyword is the most operationally useful form: an operator who wants to approve deletion up to the current state of the ledger store can simply issue `can_delete=now` without needing to query the rotation sequence first. The `rpcNOT_READY` guard protects against issuing that command before the store has completed its first rotation cycle.

## Relationship to `SHAMapStore`

`doCanDelete` is a thin orchestration layer over the `SHAMapStore` interface defined in `SHAMapStore.h`. The actual policy enforcement — whether the background deletion thread actually deletes ledgers based on `canDelete` — lives entirely in `SHAMapStoreImp`. This handler's only job is input normalization and delegation. It does not hold locks, spawn threads, or interact with the database directly.