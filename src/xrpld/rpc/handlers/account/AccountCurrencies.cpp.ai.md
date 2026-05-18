# `AccountCurrencies.cpp` — `account_currencies` RPC Handler

## Purpose

This file implements `doAccountCurrencies`, the handler for the XRPL `account_currencies` RPC command. Its job is narrow but useful: given an account identifier, enumerate the distinct currencies that account is currently capable of sending and receiving, based purely on its trust line state at a specific ledger. The result is two flat arrays of currency codes (`send_currencies` and `receive_currencies`), making it a lightweight alternative to the full `account_lines` command when callers only need to know *what* currencies are in play, not the complete trust line details.

## Validation Pipeline

The function applies a strict sequential validation chain before doing any ledger work:

1. **Presence check**: At least one of `account` or `ident` must appear in the request params. `ident` is a legacy alias for `account`, preserved for backwards compatibility with older clients; `account` takes priority.
2. **Type check**: Whichever field is present must be a JSON string, not a number or object, returning `invalid_field_error` otherwise.
3. **Ledger resolution**: `RPC::lookupLedger` resolves the target ledger — defaulting to the current validated ledger or respecting an explicit `ledger_index`/`ledger_hash` parameter. If the ledger cannot be found, the result from `lookupLedger` is returned directly (it already carries the appropriate error).
4. **Base58 decode**: `parseBase58<AccountID>` converts the string to a 20-byte `AccountID`. An invalid Base58 string injects `rpcACT_MALFORMED` into the result.
5. **Account existence**: `ledger->exists(keylet::account(accountID))` ensures the account has an entry in the ledger's state map. A missing account returns `rpcACT_NOT_FOUND`.

Each step gates on the previous; the code never proceeds to trust line enumeration until all preconditions pass.

## Send vs. Receive Determination

The core logic iterates over trust lines returned by `RPCTrustLine::getItems`, which enumerates all `RippleState` ledger objects associated with the account. For each line the handler examines two conditions:

```cpp
if (saBalance < rspEntry.getLimit())
    receive.insert(saBalance.get<Issue>().currency);
if ((-saBalance) < rspEntry.getLimitPeer())
    send.insert(saBalance.get<Issue>().currency);
```

`getBalance()` returns the balance from the queried account's perspective: positive means the account holds the peer's IOU; negative means the peer holds the account's IOU. `getLimit()` is the account's own trust limit — how much of the peer's IOU it is willing to hold. If the balance is below that limit, there is still room to receive more, so the currency goes into the `receive` set. Conversely, negating the balance gives the quantity of the account's own IOU that the peer currently holds. If that is below the peer's trust limit (`getLimitPeer()`), the peer can still accept more, meaning the account can still send — regardless of whether it holds a positive balance today.

This distinction matters architecturally: a currency appears in `send_currencies` if there is *capacity on the peer side*, not if the sender currently has a balance. An account with a zero balance on a trust line can still appear as a sender if the peer's trust limit isn't exhausted.

## `badCurrency()` Filtering

After the loop, both sets explicitly remove `badCurrency()`:

```cpp
send.erase(badCurrency());
receive.erase(badCurrency());
```

`badCurrency()` is a deliberately reserved sentinel — the three-letter code that looks like "XRP" in ASCII — which is disallowed on trust lines to prevent confusion with native XRP. Trust line data should never contain it in practice, but the erase call is a defensive guard against any malformed state that might have slipped through, ensuring it never surfaces in the API response.

## Relationship to `RPCTrustLine` and `AccountLines`

`RPCTrustLine` (declared in `TrustLine.h`) is the RPC-layer wrapper around `RippleState` SLEs. It normalises the "low account / high account" binary that the ledger uses to store trust line directionality into a consistent view from a chosen account's perspective. `RPCTrustLine::getItems` performs the directory walk over the account's owner directory to collect all associated trust lines. The same class and `getItems` method are used by `AccountLines.cpp`, which exposes the full trust line detail including quality in/out, freeze flags, and peer authorization. `AccountCurrencies` consumes a strict subset of that data — only `getBalance()`, `getLimit()`, and `getLimitPeer()` — which is why it needs no pagination and returns quickly even for accounts with many trust lines.

The ledger is accessed through `ReadView const&`, the immutable read-only interface, so the handler holds a consistent snapshot throughout and cannot observe mid-request mutations.

## Error Handling and Resource Management

All errors are returned as `Json::Value` objects, consistent with the rest of the xrpld RPC layer — there are no exceptions. The ledger is held via `std::shared_ptr<ReadView const>`, keeping the ledger snapshot alive for the duration of the call without risk of early deallocation. The two `std::set<Currency>` containers used to deduplicate currencies are stack-local and automatically cleaned up on return.