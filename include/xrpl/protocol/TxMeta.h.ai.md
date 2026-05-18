# `TxMeta` — Transaction Metadata for Validated XRPL Transactions

## Role in the System

Every transaction included in a closed XRPL ledger carries a `metaData` blob alongside the transaction itself. This blob records the transaction's result code, its ordering index within the ledger, and a detailed changelog of every ledger entry that the transaction created, modified, or deleted. `TxMeta` is the C++ object that owns and manages this metadata throughout the transaction application pipeline — from the moment a transaction begins processing through final serialization into consensus-agreed ledger state.

The class sits at the boundary between transaction execution (where ledger state is mutated tentatively) and ledger storage (where those mutations become permanent record). Its most important consumer is `ApplyStateTable::apply()`, which constructs a `TxMeta`, populates it with per-node change records, and then finalizes it for storage.

## Construction

Three constructors serve three distinct lifecycle points:

- `TxMeta(txid, ledger)` — builds an empty shell during transaction application. The `result_` field is initialized to the sentinel value `255` and `index_` to `UINT32_MAX`, both signaling "not yet finalized". The `nodes_` array pre-reserves 32 slots to avoid reallocations for typical transaction complexity.

- `TxMeta(txid, ledger, Blob const&)` — deserializes from raw binary by constructing a `SerialIter` over the blob and parsing it as an `STObject` tagged `sfMetadata`. Used when loading existing metadata from storage.

- `TxMeta(txid, ledger, STObject const&)` — constructs from an already-parsed object. Used in replay and RPC code paths where the metadata has already been deserialized into the `STObject` type hierarchy.

## Affected-Node Tracking

The heart of `TxMeta` is its `STArray nodes_` holding one entry per ledger entry touched by the transaction. Each node entry is an `STObject` whose outer field name (`sfCreatedNode`, `sfModifiedNode`, or `sfDeletedNode`) encodes the change type, and whose inner fields (`sfPreviousFields`, `sfFinalFields`, `sfNewFields`) record the before and after state.

`setAffectedNode(node_key, field_type, entry_type)` either locates an existing node record by its `sfLedgerIndex` or appends a new one. It always forcibly sets the type, which allows the caller to upgrade or reclassify an entry — for example, if processing discovers that a previously registered modified node was actually deleted.

`getAffectedNode(SLE::ref, SField&)` is the lazy-creation variant used by `ApplyStateTable` when it wants to attach field-level diff data to a node. It finds the node by key or creates it, copying the ledger-entry type from the live SLE. The overload `getAffectedNode(uint256 const&)` is the strict lookup used after a node is guaranteed to exist; it calls `UNREACHABLE` and throws `std::runtime_error` if the key is absent — a hard contract violation rather than silent failure.

## Affected Accounts

`getAffectedAccounts()` returns a `boost::container::flat_set<AccountID>` of every account implicated by the transaction's metadata. The implementation mirrors the behavior of the JavaScript `Meta#getAffectedAccounts` method (noted explicitly in a comment) to keep client libraries consistent. For each affected node it inspects `sfNewFields` (for created nodes) or `sfFinalFields` (for all others), then extracts accounts from:
- `STAccount` fields directly,
- the issuer embedded in `sfLowLimit`, `sfHighLimit`, `sfTakerPays`, and `sfTakerGets` amounts (trust-line and offer objects),
- the issuer encoded in `sfMPTokenIssuanceID` (for the MPToken amendment).

Using `flat_set` over `std::set` is a deliberate performance choice: the account list is small and built once, so contiguous storage with sorted insertion beats a tree for both construction and lookup.

## Finalization and Serialization

`addRaw(Serializer&, TER, index)` is called exactly once to finalize metadata before it enters the ledger. It stores `result_` and `index_`, then **sorts `nodes_` by `sfLedgerIndex`** before serializing. This sort is non-obvious but critical: all validators processing the same transaction must produce byte-for-byte identical metadata for the SHAMap hash to agree. Because affected nodes are accumulated in insertion order (which reflects execution order, not deterministic key order), they must be sorted before serialization. Without this sort, different validators running the same transaction would hash to different metadata blobs and consensus would break.

`getAsObject()` constructs the full `STObject sfTransactionMetaData`, embedding `sfTransactionResult`, `sfTransactionIndex`, the sorted node array, and optionally `sfDeliveredAmount` and `sfParentBatchID`. This object is both serialized for storage and emitted as JSON via `getJson()`.

## Optional Fields

`deliveredAmount_` (`std::optional<STAmount>`) records the actual amount delivered by a payment transaction when it may differ from the `Amount` field — the classic partial-payment distinction. It is set by `ApplyStateTable` after the payment engine resolves path execution, and is omitted from the serialized object when absent.

`parentBatchID_` (`std::optional<uint256>`) links a transaction to the Batch transaction that submitted it (the Batch amendment). It is propagated from `ApplyContext`, which receives it from the outermost batch-processing layer, and flows into `getAsObject()` as `sfParentBatchID` when present.

## Relationship to `ApplyStateTable`

`TxMeta` is a passive data container; all substantive metadata-building logic lives in `ApplyStateTable::apply()`. That function creates a `TxMeta`, iterates the pending ledger changes (tagged as `insert`, `modify`, or `erase`), classifies each as `sfCreatedNode`/`sfModifiedNode`/`sfDeletedNode`, calls `setAffectedNode` for each, then populates the per-node diff sub-objects by comparing original versus final SLE field values against `SField::sMD_*` metadata flags. `TxMeta` merely stores what `ApplyStateTable` computes — keeping concerns cleanly separated between change detection (in the view layer) and change recording (in the protocol type).