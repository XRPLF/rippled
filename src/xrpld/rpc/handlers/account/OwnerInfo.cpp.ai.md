# `OwnerInfo.cpp` — Legacy Owner Info RPC Handler

This file implements `doOwnerInfo`, the handler behind the `owner_info` RPC command. It is one of the oldest account-query endpoints in the rippled codebase, predating the modern `account_objects` family. Today it survives primarily for backward compatibility, as evidenced by its placement in the "Utility and legacy" category in the handler registry.

## What It Does

`doOwnerInfo` fetches a compact snapshot of an account's owned ledger objects — specifically its open offers and trust lines — from two simultaneous views of the ledger: the most recently **closed** (validated) ledger and the **current** (open, in-progress) ledger. The response is a JSON object with two keys, `accepted` and `current`, each containing the output of `NetworkOPs::getOwnerInfo` against the respective ledger state. This dual-ledger view lets callers see both settled state and any pending changes that haven't yet been incorporated into a validated ledger.

The handler is registered in `Handler.cpp` with the `NEEDS_CURRENT_LEDGER` condition, meaning the RPC framework will reject the call outright if no current ledger is available rather than letting the handler fail mid-execution. This is consistent with the other handlers (`fee`, `path_find`, `submit`) that depend on live ledger state.

## Input Handling and the `ident` Alias

The handler accepts the account identifier under either `account` or `ident`. The `ident` field is a legacy alias that predates the standardization of `account` as the canonical parameter name across the XRP Ledger API. Rather than removing `ident` and breaking old clients, the handler checks for both with a manual `isMember` guard. If neither is present, it returns `RPC::missing_field_error(jss::account)` — using the canonical name in the error even though `ident` is also valid.

Once a string is extracted, it is decoded with `parseBase58<AccountID>`, which returns `std::optional<AccountID>`. This single call handles both syntactic validation (legal Base58Check characters) and semantic validation (correct checksum, correct payload length for an AccountID). If decoding fails, `accountID` is empty and both the `accepted` and `current` fields are set to `rpcError(rpcACT_MALFORMED)` rather than bailing out early. This means a malformed input produces a structurally complete response with error objects under both ledger keys instead of a top-level error — a quirk of the legacy design.

## What `NetworkOPs::getOwnerInfo` Returns

The heavy lifting is delegated to `NetworkOPsImp::getOwnerInfo` in `NetworkOPs.cpp`. That function walks the account's owner directory (`keylet::ownerDir`) page by page, following the `sfIndexNext` chain until the entire directory is exhausted. For each entry it reads the child SLE and dispatches on its type: `ltOFFER` entries are appended to a `jss::offers` array, and `ltRIPPLE_STATE` (trust line) entries go into a `jss::ripple_lines` array. `ltACCOUNT_ROOT` and `ltDIR_NODE` entries are treated as unreachable by design and guarded with `UNREACHABLE`. The result is a flat JSON object — no pagination, no cursor — which is why `owner_info` is impractical for accounts with large object counts and why `account_objects` (with its `marker`-based pagination) supersedes it for production use.

## Design Observations

The duplicate query pattern — calling `getOwnerInfo` once for the closed ledger and once for the current ledger — is straightforward but subtly expensive: it traverses the entire owner directory twice per call. Modern handlers avoid this by accepting an explicit `ledger_index` or `ledger_hash` parameter and performing a single lookup against the requested ledger view.

The error propagation design is also notable: instead of an early return on a malformed account, both result fields are individually guarded by `accountID.has_value()`. This keeps the response shape consistent regardless of whether the account was valid, though it means the same `rpcACT_MALFORMED` error appears twice in the output for a bad input — one under `accepted` and one under `current`. Callers checking only one key might miss the error in the other.

There are no exceptions thrown or caught here. The `std::optional` from `parseBase58` and the `Json::Value` from `getOwnerInfo` carry all success and failure information purely through return values, consistent with the error-handling conventions used across the RPC handler layer.