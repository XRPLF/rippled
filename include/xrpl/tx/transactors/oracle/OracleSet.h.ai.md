# `OracleSet.h` — Price Oracle Create/Update Transactor

## Role in the System

`OracleSet.h` declares the `OracleSet` transactor, which handles both creation and in-place update of Price Oracle ledger objects on the XRP Ledger. It is one of two oracle-related transactors in the `xrpl/tx/transactors/oracle/` directory — the counterpart being `OracleDelete`, which removes oracle objects. Together they implement the full lifecycle of on-ledger price feeds as specified by XLS-47d.

A Price Oracle on the XRPL is an on-ledger object that stores a time-stamped series of token-pair prices submitted by an authorized account. It acts as a bridge between off-chain data sources (such as centralized exchange feeds) and decentralized applications that need price references without leaving the ledger. The `OracleSet` transactor makes this possible by providing a single transaction type that idempotently handles both initial creation and subsequent updates.

## Transactor Base Class and Phase Architecture

`OracleSet` publicly inherits from `Transactor` and participates in the standard three-phase validation pipeline defined by that base class: `preflight`, `preclaim`, and `doApply`. This separation is architectural: `preflight` operates without any ledger state and can run on any node for mempool filtering; `preclaim` reads (but does not modify) the current ledger to catch state-dependent errors before claiming a fee; and `doApply` performs the actual mutation. All three phases are required — none are inherited from the base no-op implementations.

The `ConsequencesFactory` is set to `Normal`, meaning this transaction competes for fee priority normally and does not act as a "blocker" that would prevent subsequent transactions from the same account from being processed in the same batch.

## `preflight` — Stateless Validation

The implementation checks purely structural constraints on the transaction fields without touching the ledger:

- `sfPriceDataSeries` must be non-empty and must not exceed `maxOracleDataSeries` entries (checked with `temARRAY_EMPTY` / `temARRAY_TOO_LARGE`).
- The optional string fields `sfProvider`, `sfURI`, and `sfAssetClass` — if present — must have lengths within their respective maximums (`maxOracleProvider`, `maxOracleURI`, `maxOracleSymbolClass`) and must not be zero-length.

The decision to leave deeper token-pair consistency checks to `preclaim` (rather than `preflight`) is intentional: those checks require reading the existing oracle object from the ledger, which is unavailable at preflight time.

## `preclaim` — Ledger-State Validation

`preclaim` does the heavy stateful validation that separates a creation from an update, and it enforces the time-freshness invariant that gives oracle data its utility:

**Timestamp freshness.** `sfLastUpdateTime` is stored in XRPL epoch seconds (offset from the Unix epoch by `epoch_offset`). The implementation converts the transaction value back to Unix time and requires it to fall within `±maxLastUpdateTimeDelta` seconds of the last closed ledger's `closeTime`. Values in the far past or future are rejected with `tecINVALID_UPDATE_TIME`. On update, the new timestamp must also be strictly greater than the existing oracle's `sfLastUpdateTime`, preventing replay or backdated feeds.

**Create vs. update divergence.** The code reads the oracle SLE keyed by `(account, sfOracleDocumentID)`. If the SLE is absent, this is a creation: `sfProvider` and `sfAssetClass` are mandatory, and any price data entry lacking `sfAssetPrice` is malformed (you cannot delete a pair from a non-existent object). If the SLE exists, it is an update: `sfProvider` and `sfAssetClass` may be omitted, but if supplied they must match the stored values (enforced by the `isConsistent` lambda) — these fields are immutable after creation.

**Token pair accounting and reserve.** During update, the code resolves the final set of pairs by merging existing pairs with the transaction's additions, updates, and deletions. Pairs in the transaction without `sfAssetPrice` signal deletion; any such pair that does not exist in the current oracle is rejected with `tecTOKEN_PAIR_NOT_FOUND`. The total resulting pair count drives a tiered owner reserve: objects with more than 5 pairs consume 2 owner count units instead of 1. The `adjustReserve` delta between old and new tier is computed here, and the submitting account's balance is checked against the updated reserve before the fee is charged.

## `doApply` — Ledger Mutation

The apply phase mirrors the create/update branching from `preclaim`:

**Update path.** All current pairs are loaded into a `std::map<pair<Currency,Currency>, STObject>` keyed by `tokenPairKey`. The transaction's entries are then applied in a loop: entries without `sfAssetPrice` erase the key, entries whose key already exists update the price and optional scale in-place, and new keys are inserted. The map is then serialized back as `sfPriceDataSeries`. This map-based approach naturally deduplicates and orders the pairs, ensuring the ledger object remains consistent regardless of the order entries were submitted.

**Create path.** A new SLE is constructed and populated. Under the `fixPriceOracleOrder` amendment, the same `std::map` sorting approach is used for the initial `sfPriceDataSeries` so that the canonical on-ledger ordering is consistent from creation onward; without this amendment the raw transaction order is preserved (legacy behavior).

**Amendment-gated fields.** The `fixIncludeKeyletFields` amendment adds `sfOracleDocumentID` to the SLE on both creation and the first update of older objects. This amendment was a bug fix to ensure the document ID is always retrievable from the object itself without requiring callers to reconstruct it from the keylet.

**Owner count.** On creation, the owner count is incremented by 1 or 2 (based on pair count). On update, only the delta between old and new tiers is applied. The owner directory is managed via `dirInsert` on creation; deletion is handled exclusively by the `OracleDelete` transactor.

## Design Observations

The use of `static` for `preflight` and `preclaim` (while `doApply` is a virtual override) reflects the XRPL framework's compile-time polymorphism pattern: `invokePreflight<OracleSet>` and `invoke_preclaim<OracleSet>` call these as static functions via template instantiation rather than virtual dispatch, avoiding vtable overhead during the validation phases where many transactions may be evaluated speculatively. The `doApply` method is virtual because it is invoked only once per committed transaction where the overhead is irrelevant.

The tiered reserve design (1 vs. 2 owner count units at the 5-pair boundary) is a pragmatic ledger storage cost model: small oracles track a handful of pairs and are economical, while larger multi-pair oracles reflecting a broader price index carry proportionally higher on-ledger cost.