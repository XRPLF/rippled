# `AccountOffers.cpp` — `account_offers` RPC Handler

This file implements the `account_offers` JSON-RPC command, which returns the list of open DEX offers currently owned by a given account. It is one of several handlers in the `src/xrpld/rpc/handlers/account/` directory that share the same paginated-directory-walk pattern: `AccountChannels.cpp`, `AccountLines.cpp`, and `AccountObjects.cpp` all follow the same structure. The file contributes two symbols to the `xrpl` namespace: a serialization helper (`appendOfferJson`) and the handler entry point (`doAccountOffers`).

## Offer Serialization: `appendOfferJson`

`appendOfferJson` converts a single `ltOFFER` ledger entry (`SLE`) into a JSON object and appends it to the output array. The non-obvious part is how the exchange rate is reported:

```cpp
STAmount const dirRate = amountFromQuality(getQuality(offer->getFieldH256(sfBookDirectory)));
obj[jss::quality] = dirRate.getText();
```

XRPL offer objects carry a `sfBookDirectory` field that stores the 256-bit hash key of the order-book directory node they belong to. The last 64 bits of that hash encode the offer's exchange rate as a fixed-point quality value. `getQuality()` extracts those 64 bits, and `amountFromQuality()` reconstructs an `STAmount` whose decimal string representation is the rate (units of `TakerPays` per unit of `TakerGets`). This avoids dividing `TakerPays` by `TakerGets` at read time; the quality is already materialized in the directory hash, so reading it is free. An optional `sfExpiration` field is only included in the output when present, keeping the response lean for non-expiring offers.

## Handler Flow: `doAccountOffers`

The handler follows a rigid validation-first pipeline enforced by early returns before any ledger iteration begins.

**Parameter validation** proceeds in this order: `account` field presence and string type; `ledger_hash`/`ledger_index` resolution via `RPC::lookupLedger`; Base58 parsing of the account address via `parseBase58<AccountID>` (returning `rpcACT_MALFORMED` on failure); account existence in the resolved ledger via `keylet::account`; and finally the `limit` field through `readLimitField`, which clamps the caller's value to the `[10, 400]` range defined in `Tuning::accountOffers` with a default of 200.

**Marker validation** is more involved than a simple string parse. The marker token is a comma-separated pair: a hex-encoded 256-bit ledger object key (`startAfter`) and a 64-bit decimal directory hint (`startHint`). Parsing uses `std::getline` with a `','` delimiter followed by `uint256::parseHex` and `boost::lexical_cast<uint64_t>`, each of which returns an error response on failure. Crucially, after parsing, the handler reads the ledger object at `startAfter` and calls `RPC::isRelatedToAccount` to verify that the object actually belongs to the requested account:

```cpp
auto const sle = ledger->read({ltANY, startAfter});
if (!sle)
    return rpcError(rpcINVALID_PARAMS);
if (!RPC::isRelatedToAccount(*ledger, sle, accountID))
    return rpcError(rpcINVALID_PARAMS);
```

This ownership check prevents a caller from crafting a marker that skips into another account's directory region, which would be a ledger-state information leak. An invalid or out-of-context marker is treated as `rpcINVALID_PARAMS` rather than a not-found error, making the distinction explicit.

## Pagination: `forEachItemAfter` and the Off-by-One Design

Iteration uses `forEachItemAfter(*ledger, accountID, startAfter, startHint, limit + 1, callback)`, which traverses the account's owner directory starting after `startAfter`. The `startHint` is the directory page index—a value recovered via `RPC::getStartHint` when the previous page's marker was created—and allows `forEachItemAfter` to seek directly to the right page rather than scanning from the root. This is the primary performance optimization for accounts with large numbers of owned objects.

The `limit + 1` request is intentional. The callback increments `count` on each invocation and applies two thresholds:

- At `count == limit`: record the current SLE's key and hint as the potential next-page marker.
- At `count <= limit`: push the SLE into the offers vector (only if `ltOFFER`).

The limit+1 item is therefore iterated but never stored in results—its sole purpose is to confirm that there *is* a next page. After iteration, the handler checks both `count == limit + 1` and `marker` before emitting the marker in the response:

```cpp
if (count == limit + 1 && marker)
{
    result[jss::limit] = limit;
    result[jss::marker] = to_string(*marker) + "," + std::to_string(nextHint);
}
```

The two-condition check is documented with an inline comment. `marker` is set when `count` first reaches `limit`, but if the directory contains exactly `limit` items and no more, the loop ends before reaching `limit + 1`, so `count` stays at `limit`. In that case, `marker` would be set but `count != limit + 1`, so no marker is returned — correct behavior.

The callback also guards against a null SLE pointer with an `UNREACHABLE` macro wrapped in `LCOV_EXCL_START/STOP`. This is a documented defensive invariant: `forEachItemAfter` only calls the callback with keys proven to exist in the directory, so a null SLE would imply ledger corruption rather than a normal error path. The `return false` stops iteration, but the code is expected to be dead in any healthy deployment.

The type filter `sle->getType() == ltOFFER` inside the callback is necessary because an account's owner directory can contain multiple object types (`ltRIPPLE_STATE`, `ltESCROW`, etc.). Only `ltOFFER` entries are collected; others are counted for pagination tracking but not serialized.

## Resource Accounting

At the end of the handler, `context.loadType = Resource::feeMediumBurdenRPC` marks the request as medium-cost for the server's rate-limiting subsystem. Scanning an account's directory—especially across multiple pages—requires proportional ledger I/O, and this classification ensures clients performing large paginated sweeps are throttled appropriately relative to simple point-lookup commands.