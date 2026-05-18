# OracleSet.cpp — Price Oracle Create/Update Transactor

`OracleSet.cpp` implements the `OracleSet` transaction for the XRPL Price Oracle feature (XLS-47d). Its purpose is to create or update an on-ledger `ltORACLE` object that stores off-chain price data — base/quote currency pairs, their prices, and an optional scaling factor — bridging external market data into decentralized applications built on the ledger. It follows the standard three-phase transactor lifecycle: `preflight` (stateless validation), `preclaim` (stateful validation), and `doApply` (ledger mutation).

## Transaction Phases

### `preflight` — Stateless Validation

The first gate operates entirely on the raw transaction bytes before any ledger state is consulted. It rejects transactions with an empty `sfPriceDataSeries` array (`temARRAY_EMPTY`) or one exceeding `maxOracleDataSeries` (10 entries, `temARRAY_TOO_LARGE`). Variable-length string fields are validated through the `isInvalidLength` lambda: `sfProvider` and `sfURI` are bounded at 256 bytes, and `sfAssetClass` at 16 bytes. Empty strings are also rejected, since the lambda checks for zero length in addition to overflow. These checks use `tem`-class codes, meaning they fail the transaction before it can affect any account's sequence number or fee balance.

### `preclaim` — Stateful Validation

The second phase reads ledger state and enforces business rules. It handles two fundamentally different scenarios — creation and update — distinguished by whether a `keylet::oracle(account, documentID)` object already exists on the ledger.

**Time window enforcement.** The `sfLastUpdateTime` field carries time in XRPL's own epoch (seconds since January 1, 2000, i.e. `epoch_offset = 946684800`). The code subtracts this offset to obtain a Unix timestamp, then compares it against the ledger's `closeTime`. The timestamp must fall within `maxLastUpdateTimeDelta` (300 seconds) of close time in both directions. This tight window ensures oracle data is fresh and prevents backdating or future-dating of price feeds. The `epoch_offset` subtraction is not just a conversion detail — it's also a safety guard: any `sfLastUpdateTime` value smaller than `epoch_offset.count()` signals a malformed submission and returns `tecINVALID_UPDATE_TIME` immediately.

**Token pair classification.** The code walks the `sfPriceDataSeries` array, classifying each entry into two `std::set` collections: `pairs` (entries that include `sfAssetPrice` and should be created or updated) and `pairsDel` (entries without a price, which signal deletion). The `tokenPairKey()` helper extracts the `(Currency, Currency)` pair as a `std::pair` map key. Entries with identical base and quote assets (`entry[sfBaseAsset] == entry[sfQuoteAsset]`) are rejected as `temMALFORMED`, as are duplicate keys — the uniqueness check using `pairs.contains(key) || pairsDel.contains(key)` detects the case where two entries in the same transaction would target the same trading pair. The `sfScale` field is optional but capped at `maxPriceScale` (20) when present.

**Create vs. update divergence.** For a new oracle, `sfProvider` and `sfAssetClass` must both be present; they are required metadata that cannot change after creation. For an update, the `isConsistent` lambda verifies that any provided `sfProvider` or `sfAssetClass` matches the existing on-ledger value — these fields are effectively immutable after creation. Additionally, the update path enforces time monotonicity: the new `sfLastUpdateTime` must be strictly greater than the previous one, preventing clock rollback attacks. The update path also reconciles the `pairsDel` set against existing pairs: after merging existing pairs into `pairs` and removing any that match `pairsDel`, any non-empty `pairsDel` indicates a deletion of a pair that doesn't exist on the ledger, returning `tecTOKEN_PAIR_NOT_FOUND`.

**Reserve accounting.** Oracle objects consume either 1 or 2 owner reserve units depending on whether the total number of tracked pairs exceeds 5. This step function (`count > 5 ? 2 : 1`) reflects the larger serialized size of bigger oracle objects. The `adjustReserve` variable captures the delta between old and new reserve counts so the reserve check uses a projected post-transaction balance. A balance below the projected reserve returns `tecINSUFFICIENT_RESERVE`.

### `doApply` — Ledger Mutation

**Update path.** The update path builds a `std::map<std::pair<Currency,Currency>, STObject>` from the existing SLE's `sfPriceDataSeries` to allow efficient keyed lookup. Each entry in the transaction then either deletes the pair (by erasing from the map), updates its price and optional scale (by mutating the existing map value), or adds a new pair. The `populatePriceData` lambda handles construction of new `STObject` entries with their inner object template applied. After the merge, the map is converted back to an `STArray` and written into the SLE. URI may be updated at this time; `sfProvider` and `sfAssetClass` cannot be changed (enforced in `preclaim`). The `sfLastUpdateTime` is always refreshed.

A notable repair: if the existing SLE is missing `sfOracleDocumentID` and the `fixIncludeKeyletFields` amendment is active, the field is backfilled. This is a forward-compatibility fixup — older oracle objects created before the amendment lack this field, and the update is the only safe opportunity to embed it.

**Create path.** A new SLE is allocated and populated with owner, provider, optional URI, asset class, update time, and the price data series. Under the `fixPriceOracleOrder` amendment, the transaction's `sfPriceDataSeries` entries are first inserted into the same sorted `std::map` before being serialized into the SLE, guaranteeing canonical on-ledger ordering regardless of submission order. Without the amendment, the raw transaction array order is preserved — this is the pre-fix behavior retained for ledger history consistency. The object is then inserted into the owner's directory via `dirInsert`, and the owner count is incremented using the same 1-or-2 reserve step function.

## Helper Functions

`tokenPairKey()` provides a consistent `(Currency, Currency)` comparison key used in both `preclaim` and `doApply`. Factoring it out avoids subtle bugs where the key extraction logic diverges between validation and application.

`setPriceDataInnerObjTemplate()` looks up the `SOTemplate` for `sfPriceData` inner objects from `InnerObjectFormats` and applies it to a freshly constructed `STObject`. This is necessary because XRPL's serialization layer requires each inner object to declare its canonical field set before fields can be set on it — without the template, field operations would behave unpredictably.

The file-scope `adjustOwnerCount()` overload is a thin wrapper that peeks the account SLE and delegates to the ledger helper, returning `false` only in the dead-code path where the account has disappeared between `preclaim` and `doApply` (hence the `LCOV_EXCL_LINE` annotation, since this is guarded by `preclaim`'s `terNO_ACCOUNT` check).

## Amendment-Gated Behavior Summary

Two amendments modify the behavior of `OracleSet`:

- **`fixPriceOracleOrder`**: On creation, sorts `sfPriceDataSeries` entries into a canonical (lexicographic by currency code) order. Without this fix, insertion order was non-deterministic across validator nodes if clients submitted entries in varying orders.
- **`fixIncludeKeyletFields`**: Embeds `sfOracleDocumentID` directly into the SLE so the object is self-describing. Before this fix, callers reconstructing the keylet had to supply the document ID from external context; with it, the field can be read directly from the SLE during traversal or RPC lookups.