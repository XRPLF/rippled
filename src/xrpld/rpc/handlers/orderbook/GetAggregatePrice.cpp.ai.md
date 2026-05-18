# `GetAggregatePrice.cpp` — RPC Handler for Multi-Oracle Price Aggregation

This file implements `doGetAggregatePrice`, the server-side handler for the `get_aggregate_price` RPC method. The method exists to give clients a manipulation-resistant price reference for any token pair by aggregating and statistically summarizing data from multiple on-chain `PriceOracle` ledger objects (defined under XLS-47). Because a single oracle could be stale or dishonest, the design expects callers to submit a list of oracle identifiers and the handler synthesizes a defensible aggregate from whatever valid prices it can recover.

## The `Prices` Bimap and Why It Was Chosen

The central data structure is a `boost::bimap` aliased as `Prices`:

```cpp
using Prices =
    bimap<multiset_of<std::uint32_t, std::greater<std::uint32_t>>, multiset_of<STAmount>>;
```

The left side indexes by `lastUpdateTime` in descending order; the right side indexes by `STAmount` in ascending order. Two orthogonal sorting requirements exist simultaneously: time-window filtering needs prices sorted newest-first so stale entries can be erased by calling `prices.left.upper_bound(upperBound)` through `prices.left.end()`, while statistical operations (median, trimmed mean) need prices sorted by value. Keeping two separate containers synchronized manually would be error-prone; the bimap makes both views automatically consistent — erasing an entry from one side removes it from the other.

## Historical Chain Walking in `iteratePriceData`

A non-obvious complication arises when an oracle exists on the ledger but was last updated without touching the specific token pair requested. In that case the current `sfPriceDataSeries` won't have an entry for that pair, even though an older version of the oracle did. `iteratePriceData` handles this by following each oracle's transaction history backward through the ledger, up to three hops (`maxHistory = 3`).

The function maintains two conceptually separate pointers as raw `STObject const*`:

- **`oracle`** — points to the object that has `sfPriceDataSeries`. Initially this is the live `SLE`; after the first history hop it becomes either `sfNewFields` (if the previous transaction created the oracle) or `sfFinalFields` (if it modified one) inside a `CreatedNode`/`ModifiedNode` metadata entry.
- **`chain`** — points to the object that has `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` for the next hop. Initially it equals `oracle` (both point to the live SLE), but diverges once inside transaction metadata, where those navigation fields live on the `ModifiedNode`/`CreatedNode` wrapper rather than on the nested field object.

The `prevChain == chain` guard at the top of the loop detects the case where the inner search through `sfAffectedNodes` failed to find an `ltORACLE` entry at all, so the loop exits cleanly rather than looping forever. An `isNew` flag set when `sfNewFields` is present short-circuits on the first history lookup: if the very first previous transaction was a create, there is nothing further behind it to examine.

This design avoids fetching unnecessary ledgers — the callback `f` returns `true` the moment it finds a matching price, so the loop stops immediately without retrieving history that won't be used.

## Statistical Computation in `getStats`

`getStats` takes a begin/end pair of `Prices::right_const_iterator` (the value-sorted view) and returns a tuple of mean, sample standard deviation, and count. It uses `std::accumulate` twice: once to sum the `STAmount` values for the mean, then again to accumulate the squared deviations from that mean. The standard deviation uses the `n-1` denominator (Bessel's correction), appropriate since these are samples from a broader population of potential oracle reports. `root2` computes the square root via XRPL's `Number` type, which carries enough precision for financial arithmetic.

## Median and Trimmed Mean

After `getStats` reports full-dataset statistics, the handler computes the median directly from the right (price-ordered) view of the bimap. For even-length datasets it averages the two middle elements; for odd lengths it selects the middle element. The `itAdvance` lambda wraps `std::advance` to return an iterator by value, making the inline arithmetic readable.

If the caller supplied a `trim` parameter (1–25, validated against `maxTrim = 25`), the trimmed statistics are computed by passing `prices.right.begin() + trimCount` and `prices.right.end() - trimCount` to `getStats`. This symmetric trim removes the same number of extreme low and high prices from both ends of the sorted set, reducing the influence of outliers without discarding the whole dataset.

## Input Validation Strategy

The handler uses `std::variant<std::uint32_t, error_code_i>` as the return type for its local `getField` lambda, making parse failure first-class rather than exception-driven. The `getCurrency` lambda is the exception to this pattern — it wraps `currencyFromJson` in a try-catch because that function throws on malformed input. Both approaches convert their errors into `RPC::inject_error` calls before returning early. Up to 200 oracle references are accepted (`maxOracles = 200`); exceeding this or supplying an empty list both produce `rpcORACLE_MALFORMED`. Trim of zero is also rejected (`trim == 0` means no trimming was intended but the field was supplied with an invalid value).

## Relationship to the Oracle Object Model

Each oracle is identified by `(account, oracle_document_id)`, resolved to a ledger object via `keylet::oracle`. The `sfPriceDataSeries` field on that object is an array of `{sfBaseAsset, sfQuoteAsset, sfAssetPrice, sfScale}` tuples. The `sfAssetPrice` is a raw `uint64` mantissa, and `sfScale` is an unsigned exponent whose negation is passed to `STAmount` so that `price * 10^(-scale)` is the actual value. This keeps oracle values as integers on-chain while representing fractional prices losslessly. The handler never interprets `sfAssetPrice` as a signed number, matching the on-chain convention enforced by `OracleSet`.