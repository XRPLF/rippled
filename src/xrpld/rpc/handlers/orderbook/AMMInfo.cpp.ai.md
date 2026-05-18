# `AMMInfo.cpp` — `doAMMInfo` RPC Handler

## Role in the System

This file implements the `doAMMInfo` RPC endpoint, which allows JSON-RPC callers to inspect an Automated Market Maker (AMM) pool on the XRPL. The response bundles together the pool's reserve balances, the current trading fee, vote slot state, and the active auction slot — everything a client or DEX front-end needs to reason about an AMM's current state. It sits in `src/xrpld/rpc/handlers/orderbook/`, alongside other orderbook-oriented handlers like `BookOffers` and `GetAggregatePrice`.

---

## Helper Functions

### `getAsset`

`assetFromJson` (from `AMMCore`) throws `std::runtime_error` when given malformed input. Rather than letting that exception bubble through the RPC layer uncontrolled, `getAsset` wraps the call in a `try/catch` and translates any parse failure into `Unexpected(rpcISSUE_MALFORMED)`. This is the standard XRPL RPC convention: structured error codes at the boundary, never raw exceptions. The `Expected<Asset, error_code_i>` return type makes the error path type-safe and explicit at each call site.

### `to_iso8601`

XRPL's internal clock (`NetClock`) counts seconds since the Ripple epoch — 2000-01-01 00:00:00 UTC — while the standard Unix epoch starts in 1970. `to_iso8601` bridges this by adding `epoch_offset` (946,684,800 seconds) to the duration before constructing a `system_clock::time_point` and formatting it with the Howard Hinnant `date` library. This conversion is used exclusively for the auction slot's `sfExpiration` field, ensuring the expiration timestamp is presented in a universally readable format.

---

## `doAMMInfo` — Parameter Validation Architecture

The handler begins with a standard `RPC::lookupLedger` call, which resolves the target ledger (open, validated, or a specific historical ledger) and handles all ledger-level errors before any AMM logic runs.

Parameter extraction and validation is encapsulated in a nested lambda `getValuesFromContextParams` that returns `Expected<ValuesFromContextParams, error_code_i>`. This pattern keeps error propagation explicit without requiring output parameters or exception abuse; each failure path returns `Unexpected(error_code)` and the caller checks with `if (!r)` before destructuring.

The core constraint is that callers must supply **either** the `asset`+`asset2` pair **or** `amm_account`, but not both — captured by the `invalid` lambda:

```cpp
(params.isMember(jss::asset) != params.isMember(jss::asset2)) ||
(params.isMember(jss::asset) == params.isMember(jss::amm_account))
```

There is a subtle API version sensitivity here. For `apiVersion < 3`, the `invalid` check fires **before** any individual fields are parsed. For `apiVersion >= 3`, the check fires **after** parsing `asset`, `asset2`, and `amm_account`. The comment in the code notes these are "identical check" blocks placed at different positions intentionally — the ordering controls which error fires first when a caller provides conflicting inputs under older vs. newer API versions, preserving backward-compatible error sequencing.

When `amm_account` is provided, the handler walks the ledger: it reads the account's `SLE` (state ledger entry), extracts `sfAMMID` from it, and uses that to derive the AMM keylet. If `sfAMMID` is zero, the account exists but is not an AMM account, and `rpcACT_NOT_FOUND` is returned. The two lookup paths — `keylet::amm(*asset1, *asset2)` and `keylet::amm(*ammID)` — converge at the same AMM SLE, and if the lookup was by ID alone, the assets are back-filled from the SLE's `sfAsset`/`sfAsset2` fields.

An `XRPL_ASSERT` enforces the internal consistency invariant that after validation, exactly one of (`asset1`+`asset2`) or `ammID` is set — catching any future refactoring that breaks the mutual-exclusion logic at debug time.

---

## Response Construction

**Pool balances** are fetched via `ammPoolHolds` with `FreezeHandling::fhIGNORE_FREEZE`. This is intentional: an AMM cannot suspend operations simply because an issuer has frozen a trust line. The freeze state is reported separately — `asset_frozen` and `asset2_frozen` fields are appended for non-XRP assets only, since XRP has no freeze mechanism. This design means callers always see the real pool depth and must independently decide how to act on freeze status.

**LP token balance** is context-dependent. When the request includes an `account` parameter, `ammLPHolds` returns that specific account's liquidity provider balance (how many LP tokens the account holds). Without `account`, the total `sfLPTokenBalance` from the AMM SLE is returned — the aggregate outstanding supply.

**Vote slots** are iterated from `sfVoteSlots` if present and serialized as a JSON array with per-voter account, fee, and weight. The array is only included in the response if non-empty, keeping the output clean for AMMs without active governance.

**Auction slot** serialization has a layered presence check. First, `sfAuctionSlot` must exist on the AMM object. An assertion guards that once the `fixInnerObjTemplate` amendment is active, the field is always present. Second, within the slot, `sfAccount` must be set — the slot is "active" only if someone holds it. An unowned slot has no account and is silently omitted from the response.

When the auction slot is active, `ammAuctionTimeSlot` computes which of the 20 time intervals (each 72 minutes of a 24-hour cycle) the slot is currently in, using `parentCloseTime` from the ledger header. If the slot has expired, `ammAuctionTimeSlot` returns `std::nullopt` and the handler emits `AUCTION_SLOT_TIME_INTERVALS` (20) as `time_interval` — a sentinel value indicating expiry rather than omitting the field. Authorized accounts (`sfAuthAccounts`) are listed if present, since they benefit from the discounted fee during the slot period.

**Ledger identification** follows XRPL conventions: `ledger_current_index` is appended only when neither `ledger_index` nor `ledger_hash` was specified by the caller (meaning the current open ledger was used), and `validated` is set by querying `LedgerMaster`.