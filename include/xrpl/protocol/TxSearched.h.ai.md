# `TxSearched.h`

This file defines a single scoped enumeration, `TxSearched`, that communicates the coverage completeness of a transaction search operation across the XRPL node's local ledger history.

## Why It Exists

When a client queries a rippled node for a transaction by hash, the node may not have a complete ledger history. This creates a fundamental ambiguity: "transaction not found" could mean the transaction genuinely does not exist, or it could mean the node simply does not have the ledgers that would contain it. The `TxSearched` enum is a formal discriminant that resolves this ambiguity in the return type of every transaction-fetch API that supports ranged searches.

## The Three States

```cpp
enum class TxSearched { All, Some, Unknown };
```

- **`All`** — the node searched its entire local history for the requested ledger range and confirmed the transaction is absent. The search was exhaustive. A client receiving this can be confident the transaction was not included in that range.
- **`Some`** — the node attempted the search but its local ledger store is incomplete for the requested range. Some ledgers in the range were missing from the database. Absence of the transaction in the results is not conclusive.
- **`Unknown`** — the search was performed without a ledger range constraint, a deserialization error occurred, or coverage information was otherwise unavailable. The `searched_all` field is suppressed in the JSON response when this state is active.

## Usage Pattern

`TxSearched` is the alternative type in a `std::variant` that pairs with the actual transaction result. In `TransactionMaster::fetch()` and `RelationalDatabase::getTransaction()`, the return type is:

```cpp
std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
```

When a transaction is found, the variant holds the `(Transaction, TxMeta)` pair. When it is not found, the variant holds a `TxSearched` value describing why — essentially encoding both the "not found" signal and the confidence level in a single type-safe value. This forces callers to explicitly handle the coverage-completeness question rather than ignoring it.

The RPC handler `doTxHelp()` in `Tx.cpp` inspects the variant directly:

```cpp
if (auto e = std::get_if<TxSearched>(&v))
{
    result.searchedAll = *e;
    return {result, rpcTXN_NOT_FOUND};
}
```

And `populateJsonResponse()` surfaces it to clients only when meaningful:

```cpp
if (error.toErrorCode() == rpcTXN_NOT_FOUND && result.searchedAll != TxSearched::Unknown)
{
    response[jss::searched_all] = (result.searchedAll == TxSearched::All);
    error.inject(response);
}
```

The `searched_all` field in the JSON response collapses `TxSearched` into a boolean — `true` for `All`, `false` for `Some` — and is omitted entirely when the state is `Unknown`. This means clients can unambiguously distinguish a confirmed miss from an inconclusive one, which is important for wallets and explorers that rely on node APIs to confirm transaction finality.

## Design Choice: Enum Over Boolean

Using a three-valued enum rather than `std::optional<bool>` makes the `Unknown` case explicit and distinct from both `true` and `false`. It avoids the common pitfall of treating `nullopt` as carrying the same semantics as `false` (not-found-completely), and it allows the database layer in `Node.cpp` and `SQLiteDatabase.cpp` to independently compute and communicate coverage without the caller needing to know how that was determined. The enum is defined in the `protocol` layer rather than the `app` or `rdb` layer precisely because it is part of the observable protocol contract exported to RPC clients.