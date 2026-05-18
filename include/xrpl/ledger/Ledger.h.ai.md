# `include/xrpl/ledger/Ledger.h`

## Role in the System

`Ledger.h` declares the `Ledger` class — the central data structure of the entire rippled server. Every validated or in-progress ledger is an instance of this class. It owns the full account state of the XRP Ledger at a given sequence number, plus the complete set of transactions that produced that state, and exposes both through a layered view hierarchy that the rest of the codebase depends on for transaction processing, consensus, and data access.

## Structural Composition

A `Ledger` holds exactly two `SHAMap` members: `stateMap_` and `txMap_`. Both are Merkle–radix trees with a fan-out of 16. `stateMap_` contains all serialized ledger state entries (SLEs) — account roots, trust lines, order book pages, escrows, amendments, fee settings, and more. `txMap_` contains serialized transaction blobs paired with their execution metadata, keyed by transaction ID. Every query about account balances, object existence, or transaction results ultimately reaches one of these two maps.

The maps are declared `mutable` despite conceptual immutability because `setFull()` and the iterator implementations need to access them in `const` contexts. This is an acknowledged pattern in the code: the `SHAMap` state can evolve (e.g., fetching remote nodes during sync) without changing the logical ledger data.

## Inheritance Design

`Ledger` inherits from three base classes:

- `DigestAwareReadView` (extends `ReadView`): provides the standard read interface for ledger state and transactions, plus a `digest()` method that returns the Merkle hash of an individual state item. This extends the basic read contract and is needed by `CachedView` to detect stale cache entries.
- `TxsRawView` (extends `RawView`): exposes raw mutation operations — `rawInsert`, `rawErase`, `rawReplace`, and `rawDestroyXRP` on the state map, plus `rawTxInsert` on the transaction map. This interface is used only while the ledger is mutable, during transaction application.
- `CountedObject<Ledger>`: intrusive reference counting for diagnostics and resource leak detection.

The class is marked `final` because its constructors call virtual functions (specifically `setup()` through the virtual dispatch chain), making subclassing unsafe.

## Mutable/Immutable Lifecycle

The documentation block in the header captures the essential concurrency contract: a mutable `Ledger` must not be shared, while an immutable one can be shared freely without locks. This eliminates locking overhead for the overwhelmingly common read path.

The transition happens in `setImmutable(bool rehash)`. When called (with `rehash = true`, the default), it computes the SHAMap hash of both maps, assembles those hashes into the `LedgerHeader`, calls `calculateLedgerHash()` to produce the canonical ledger hash, then locks both SHAMaps into immutable state and calls `setup()` to populate the `fees_` and `rules_` members from the state entries in `stateMap_`. After this point, no mutations are permitted — any attempt to call `rawInsert` et al. on an immutable SHAMap will assert.

`setAccepted()` is a coordinated sequence that sets timing fields and then delegates to `setImmutable()`. It carries an assertion (`!open()`) because only closed ledgers can be accepted.

## Constructor Paths

Five distinct constructors serve different creation scenarios:

**Genesis (`create_genesis_t`)**: Constructs ledger sequence 1 from scratch. Seeds the master account by deterministically deriving its ID from the string `"masterpassphrase"`, credits it with `INITIAL_XRP`, inserts the `sfAmendments` object for any pre-enabled amendments, and inserts the fee schedule SLE (using either drops-native or legacy fee field format depending on whether `featureXRPFees` is among the amendments). Ends with `setImmutable()`.

**JSON-loaded (`LedgerHeader + bool& loaded + acquire`)**: Restores a ledger from its header. Constructs both SHAMaps with known root hashes from the header, then calls `fetchRoot()` on each. If roots are missing from the node store, the `loaded` out-parameter is set to `false` and, when `acquire = true`, triggers async acquisition via `family.missingNodeAcquireByHash()`. This path creates an already-immutable ledger.

**Successor (`Ledger const& previous + closeTime`)**: Creates the next ledger in the chain. The new `txMap_` starts empty (a fresh SHAMap for the new ledger's transactions), while `stateMap_` is constructed by deep-copying the previous ledger's state map — `SHAMap(prevLedger.stateMap_, true)` where the `true` flag requests a copy-on-write snapshot. The header is populated with incremented sequence, updated parent hash, parent close time, and recalculated close time resolution.

**Header-only (`LedgerHeader + Rules + Family`)**: Creates an immutable placeholder holding only the header, used for partial/skeleton ledgers from the database. The hash is computed immediately from the header fields.

**Database placeholder (`ledgerSeq + closeTime + Rules + Fees + Family`)**: Creates a mutable empty ledger for database reconstruction scenarios; calls `setup()` to initialize fee/rules state from whatever state entries may already exist.

## Key Mutation Methods

`rawDestroyXRP(XRPAmount fee)` is defined inline as `header_.drops -= fee`. This implements the XRPL's deflationary model: transaction fees are burned rather than redistributed, permanently reducing `drops` in the header. There is no escrow account or validator payment.

`rawTxInsertWithHash()` extends the `TxsRawView` interface by returning the hash of the SHAMap leaf node that stores the transaction. This enables a direct-lookup optimization: callers can bypass SHAMap tree traversal entirely when fetching a transaction they just inserted, using the leaf hash as a direct node store key.

`addSLE()` is a convenience wrapper (returning bool to signal failure) used during ledger construction from external data sources.

## Negative UNL

Three methods — `negativeUNL()`, `validatorToDisable()`, and `validatorToReEnable()` — read the Negative UNL state from the ledger's state map. The Negative UNL is a consensus-level mechanism for temporarily removing chronically offline validators without breaking liveness. `updateNegativeUNL()` must be called exactly at flag ledgers (sequence divisible by 256) and before any `UNLModify` transaction is applied; the comments enforce this timing contract explicitly.

`isFlagLedger()` and `isVotingLedger()` identify the two special positions in the 256-ledger cycle: flag ledgers carry out amendment votes, fee votes, and NegUNL updates; voting ledgers (flag − 1) are where validators cast their preferences.

## `setFull()` and Node Store Semantics

`setFull()` is declared `const` because fullness is not consensus data — it is a local node's storage policy, telling the node store that all SHAMap nodes for this ledger should be retained in durable storage. Marking it `const` acknowledges that this property can vary across nodes holding the identical ledger.

## Iterator Implementation

`sles_iter_impl` and `txs_iter_impl` are private nested classes (defined in the `.cpp` file) that bridge `SHAMap::const_iterator` to the abstract `iter_base` interfaces declared in `ReadView`. They deserialize `SHAMapItem` blobs on demand via `deserializeTx` and `deserializeTxPlusMeta`. The static deserializers are split into single-object and pair forms because open ledgers store transactions without metadata, while closed ledgers always store both.

## `CachedLedger`

The alias `using CachedLedger = CachedView<Ledger>` creates the standard shareable ledger type used at rest in most of the server. `CachedView` layers an `unordered_map` keyed on SLE key in front of the `Ledger`, avoiding repeated deserialization of frequently accessed state entries. This is the type that callers like the transaction engine and RPC handlers typically hold, not a raw `Ledger`.

## Concurrency and Resource Ownership

The deleted copy and move constructors enforce that `Ledger` objects are always owned through `std::shared_ptr`, consistent with `enable_shared_from_this`. The single `mutex_` member protects fee-variable access in a narrow window before immutability is fully established. Once `setImmutable()` completes, all shared readers proceed without locking. The asymmetry — mutable ledgers are exclusively owned, immutable ones are freely shared — eliminates contention from the hot read path entirely.