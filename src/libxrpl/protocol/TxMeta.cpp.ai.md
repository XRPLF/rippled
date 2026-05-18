# `TxMeta.cpp` — Transaction Metadata Implementation

## Role and Purpose

Every transaction applied to the XRP Ledger produces a metadata record that describes exactly which ledger entries were created, modified, or deleted and what their field values were before and after the transaction. `TxMeta` is the class that constructs, accumulates, and serializes this record. It is a pure protocol artifact: it does not execute any business logic itself, but rather serves as the structured container that `ApplyStateTable` fills in as it processes each ledger entry modification, and that the network then stores alongside the transaction in the closed ledger.

The metadata has three core pieces: the `sfAffectedNodes` array (a list of `sfCreatedNode`, `sfModifiedNode`, and `sfDeletedNode` entries), a `sfTransactionResult` code (the `TER` value mapped to a `uint8_t`), and a `sfTransactionIndex` giving the transaction's position within its ledger. Two optional fields round it out: `sfDeliveredAmount` (for payment transactions that may deliver less than the requested amount) and `sfParentBatchID` (for inner transactions processed as part of a `Batch` transaction).

## Construction: Three Entry Points

The three constructors serve different lifecycle phases.

The **two-argument constructor** (`transactionID`, `ledger`) builds an empty metadata object during transaction application. `result_` is initialized to 255 (a sentinel marking it as unset) and `index_` to `UINT32_MAX`. The `nodes_` array pre-reserves 32 slots, which is enough for all but the most complex transactions and avoids reallocation during node accumulation.

The **`Blob` constructor** deserializes previously persisted metadata bytes off disk or over the wire. It constructs a `SerialIter` over the raw bytes, materializes a full `STObject` tagged `sfMetadata`, then extracts the three required fields. This path is used when loading closed-ledger transaction records for RPC queries or transaction history.

The **`STObject` constructor** handles the case where metadata is already parsed as part of a broader ledger object (e.g., during replay). A subtle quirk: the member initializer list calls `obj.getFieldArray(sfAffectedNodes)` to initialize `nodes_` by value, but the constructor body immediately overwrites `nodes_` again via a `dynamic_cast<STArray const*>(obj.peekAtPField(...))`. The redundant first extraction is a mild inefficiency; the real initialization is the body assignment. The `XRPL_ASSERT` guards the cast, ensuring the field's runtime type matches the expected `STArray` — a defense against corrupted or malformed ledger data.

Both deserialization constructors delegate to `setAdditionalFields()` (defined inline in the header) which conditionally reads `sfDeliveredAmount` and `sfParentBatchID` if present — fields that are optional by protocol.

## Node Accumulation: `setAffectedNode` and `getAffectedNode`

The accumulation pattern in `ApplyStateTable` follows a two-phase approach. First, `setAffectedNode(ledgerIndex, sfType, nodeType)` is called to register that a ledger entry is affected by the transaction and to categorize it (created, modified, or deleted). If an entry with the same `sfLedgerIndex` already exists in `nodes_`, its type field is updated in place. Otherwise a new `STObject` is pushed. Second, `getAffectedNode(ledgerIndex)` is called to retrieve the same node object and attach `sfPreviousFields`, `sfFinalFields`, or `sfNewFields` sub-objects describing actual field deltas.

Both methods do a linear scan over `nodes_`. This is a deliberate choice: the list of affected nodes per transaction is bounded and small (the reservation of 32 slots is rarely exceeded), so a hash map would add overhead without benefit.

The `getAffectedNode(uint256)` overload — which takes a hash rather than an `SLE::ref` — is an internal variant that asserts the node must already exist. It is only called after `setAffectedNode` has guaranteed registration. If the node is absent anyway, `UNREACHABLE` fires (an instrumentation abort in debug builds) and a `std::runtime_error` is thrown as a last-resort safety net. The `LCOV_EXCL` markers around this path indicate it is intentionally excluded from coverage metrics because correct callers will never trigger it.

## `getAffectedAccounts()` — Cross-Type Account Extraction

This method assembles the set of `AccountID` values whose ledger state was touched by the transaction, returning a `boost::container::flat_set` (sorted, compact, cache-friendly). The comment explicitly notes it must match the behavior of the JavaScript `Meta#getAffectedAccounts` method, establishing a cross-platform invariant.

The logic handles three distinct field shapes: direct `STAccount` fields (e.g., `sfAccount`, `sfDestination`), `STAmount` fields for trust-line limits and order-book amounts that embed issuer IDs (`sfLowLimit`, `sfHighLimit`, `sfTakerPays`, `sfTakerGets`), and `sfMPTokenIssuanceID` — a 192-bit bitstring encoding for the MPToken feature from which an `AccountID` issuer is recovered via `MPTIssue`. For `sfCreatedNode` entries the scan covers `sfNewFields`, while modified and deleted nodes use `sfFinalFields`. This asymmetry reflects the metadata schema: newly created nodes record their initial state in `sfNewFields`, while modified/deleted nodes record their last-known state in `sfFinalFields`.

Each type-cast is paired with an `XRPL_ASSERT` to validate that the dynamic type matches expectations, followed by a null guard that suppresses any possible crash in production builds where assertions are no-ops.

## Serialization: `addRaw` and `getAsObject`

`addRaw()` is the terminal step of metadata construction. It stamps `result_` and `index_` with the actual `TER` outcome and ledger position, then **sorts `nodes_` by `sfLedgerIndex`** before serializing. This sort is critical for determinism: two validators applying the same transaction must produce byte-identical metadata. Without sorting, the order in which `ApplyStateTable` happens to visit modified entries (which is map-iteration order, not guaranteed stable across implementations) would produce non-deterministic blobs.

`getAsObject()` assembles the `STObject` representation unconditionally. It asserts `result_ != 255` to catch any call made before `addRaw()` has finalized the result — emitting metadata with an uninitialized result would silently corrupt the ledger record. The optional `deliveredAmount_` and `parentBatchID_` fields are written only when present.

## Relationship to `ApplyStateTable`

`TxMeta` and `ApplyStateTable` are tightly coupled. The entire accumulation workflow lives in `ApplyStateTable::apply()`: a fresh `TxMeta` is constructed, optional fields are set, then for each pending ledger entry change the table calls `setAffectedNode` followed by `getAffectedNode` to attach field-level deltas. Once all entries are processed, `addRaw` serializes the metadata into a `Serializer`, and both the raw bytes and the `TxMeta` object itself are returned — the former stored in the ledger database, the latter available for diagnostics and RPC responses.