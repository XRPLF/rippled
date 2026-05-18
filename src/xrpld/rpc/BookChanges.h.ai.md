# `BookChanges.h` — Per-Ledger Order Book Change Aggregator

## Role in the System

`BookChanges.h` provides the single template function `computeBookChanges`, which scans every transaction in a closed ledger, extracts offer-crossing activity from transaction metadata, and aggregates the results into OHLCV-style (Open, High, Low, Close, Volume) market data per currency pair. The output drives two distinct consumption paths: the `book_changes` WebSocket subscription stream (pushed to subscribers on every validated ledger in `NetworkOPs.cpp`) and the `book_changes` RPC endpoint (on-demand via `handlers/orderbook/BookChanges.cpp`).

## Why a Header-Only Template

The function is parameterized over `L`, the ledger type, rather than being bound to a concrete `ReadView`. This means it compiles against any type that exposes `.txs` (an iterable of `(STTx, TxMeta)` pairs) and `.header()` (a struct with `seq`, `hash`, `validated`, and `closeTime`). The design avoids introducing a virtual dispatch layer and allows the same logic to work with both production ledger objects and lightweight test fixtures without an explicit interface hierarchy.

## How Offer Activity Is Extracted

The function iterates over the `sfAffectedNodes` array in each transaction's metadata. Several filters are applied aggressively before any computation:

- Only nodes with `sfLedgerEntryType == ltOFFER` are considered; all other on-ledger objects are ignored.
- `sfCreatedNode` entries are skipped. A freshly created offer that wasn't crossed has no volume yet.
- Nodes missing either `sfFinalFields` or `sfPreviousFields` are skipped. Without both snapshots, the delta cannot be computed. The code comments note this is typical of *cancelled* offers where no actual crossing occurred, so skipping them is semantically correct.
- Offers deleted by an explicit `OfferCancel` transaction (or by `sfOfferSequence` in an `OfferCreate`) are filtered out by comparing the offer's `sfSequence` against the cancel target. This avoids attributing volume to a removal that had no economic activity.

For nodes that pass all filters, the volume delta is `finalFields.sfTakerGets - previousFields.sfTakerGets` (and similarly for `sfTakerPays`). Because `FinalFields` reflects the state after the transaction and `PreviousFields` the state before, the difference precisely captures how much of each side was consumed by crossing.

## Canonical Pair Key and Rate Convention

The tally map is keyed by a string like `"XRP_drops|USD/rGateway"` or `"USD/rA|BTC/rB"`. The ordering rule is: XRP always occupies the first position; if neither asset is XRP, the lexicographically smaller asset string comes first. This canonical ordering, controlled by the `noswap` boolean, means a single book is represented by exactly one key regardless of which direction individual offers were placed. Both sides of each trade contribute to the same accumulator.

The exchange rate is `divide(first, second, noIssue())`. Since a rate is a dimensionless ratio between two amounts, no real issuer context applies — `noIssue()` (a static sentinel with `noCurrency()` / `noAccount()`) satisfies the `STAmount::divide` interface while making clear the result is not attributable to any specific IOU issuer.

## OHLCV Tally Structure

Each entry in the `tally` map is a 7-element tuple:

| Index | Meaning |
|---|---|
| 0 | Side A cumulative volume |
| 1 | Side B cumulative volume |
| 2 | Highest rate seen (H) |
| 3 | Lowest rate seen (L) |
| 4 | Rate of the first trade (O) — set once, never updated |
| 5 | Rate of the most recent trade (C) — overwritten each iteration |
| 6 | Optional `uint256` domain ID |

Open and close reflect transaction order within the ledger as iterated; they are not timestamps. The domain field supports the permissioned DEX extension where offers can be tagged with a `sfDomainID`, enabling market data to be segregated by permissioned pool. The domain of the *last* processed trade for a given pair wins, which is consistent since all offers in one permissioned book share the same domain.

## JSON Output Shape

The returned `Json::Value` contains `type`, `validated`, `ledger_index`, `ledger_hash`, `ledger_time`, and a `changes` array. Each element of `changes` carries `currency_a` / `currency_b` (for classic IOU/XRP pairs) or `mpt_issuance_id_a` / `mpt_issuance_id_b` (for MPT-based pairs), along with `volume_a`, `volume_b`, `high`, `low`, `open`, `close`, and an optional `domain`. The asset-type dispatch uses `STAmount::asset().visit(...)` with two lambda overloads, keeping IOU and MPT serialization paths cleanly separated without a type-tag check.

## Defensive Patterns and Invariants

The zero-division guard (`if (second == beast::zero) continue`) prevents a pathological rate computation if metadata is somehow malformed. The comment labels it "defensively programmed, should (probably) never happen" — the hedge is honest, since offer crossing logic in the transaction engine should never produce a zero-pays delta alongside a non-zero-gets delta, but the metadata consumer has no way to enforce that invariant independently. Similarly, the final absolute-value normalization (`if (first < beast::zero) first = -first`) corrects for deltas that come out negative when the subtraction order produces a negative intermediate.