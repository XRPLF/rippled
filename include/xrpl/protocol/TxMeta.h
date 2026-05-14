#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

#include <optional>

namespace xrpl {

/** Metadata record for a single transaction applied to a closed XRPL ledger.
 *
 *  Every validated transaction carries a metadata blob alongside the
 *  transaction itself. This class owns that metadata: the result code, the
 *  transaction's ordinal position within the ledger, and the `AffectedNodes`
 *  array — one entry per ledger object that was created, modified, or deleted.
 *
 *  The primary lifecycle is: construct empty via `TxMeta(txid, ledger)`,
 *  accumulate node records through `setAffectedNode` / `getAffectedNode` (both
 *  called by `ApplyStateTable`), then finalize with a single call to
 *  `addRaw()`. Calling `getAsObject()` or `getJson()` before `addRaw()` will
 *  trigger an assertion because `result_` still holds the sentinel value 255.
 *
 *  The two deserializing constructors handle the opposite direction: loading
 *  existing metadata from raw bytes (database / wire) or from an already-parsed
 *  `STObject` (ledger replay, RPC).
 *
 *  @note `TxMeta` is a passive data container. All change-detection and
 *      field-diff logic lives in `ApplyStateTable::apply()`, which drives the
 *      accumulation protocol.
 *  @note `addRaw()` sorts `nodes_` by `sfLedgerIndex` before serializing.
 *      This sort is consensus-critical: validators must produce byte-identical
 *      metadata blobs for the SHAMap hash to agree.
 */
class TxMeta
{
public:
    /** Construct an empty metadata shell for a transaction being applied.
     *
     *  Initializes `result_` to 255 and `index_` to `UINT32_MAX` as sentinels
     *  indicating the object has not yet been finalized. Pre-reserves 32 slots
     *  in `nodes_` to absorb typical transaction complexity without
     *  reallocation. Must be finalized by `addRaw()` before serialization.
     *
     *  @param transactionID Hash of the transaction this metadata describes.
     *  @param ledger        Sequence number of the ledger in which the
     *      transaction is included.
     */
    TxMeta(uint256 const& transactionID, std::uint32_t ledger);

    /** Deserialize metadata from a raw byte blob.
     *
     *  Wraps `vec` in a `SerialIter`, materializes an `STObject` tagged
     *  `sfMetadata`, and extracts `sfTransactionResult`, `sfTransactionIndex`,
     *  and `sfAffectedNodes`. Also reads optional `sfDeliveredAmount` and
     *  `sfParentBatchID` via `setAdditionalFields`. Used when loading existing
     *  metadata from the ledger database or from the wire.
     *
     *  @param txID   Hash of the transaction this metadata describes.
     *  @param ledger Sequence number of the containing ledger.
     *  @param vec    Raw serialized metadata bytes.
     */
    TxMeta(uint256 const& txID, std::uint32_t ledger, Blob const&);

    /** Construct from an already-parsed STObject.
     *
     *  Extracts `sfTransactionResult`, `sfTransactionIndex`, and
     *  `sfAffectedNodes` from `obj`, plus any optional fields. Used during
     *  ledger replay and in RPC code paths where the metadata has already been
     *  deserialized into the `STObject` type hierarchy.
     *
     *  @param txID   Hash of the transaction this metadata describes.
     *  @param ledger Sequence number of the containing ledger.
     *  @param obj    Fully deserialized metadata object (e.g. `sfMetadata`).
     */
    TxMeta(uint256 const& txID, std::uint32_t ledger, STObject const&);

    /** Return the hash of the transaction this metadata record describes. */
    [[nodiscard]] uint256 const&
    getTxID() const
    {
        return transactionID_;
    }

    /** Return the sequence number of the ledger that included this transaction. */
    [[nodiscard]] std::uint32_t
    getLgrSeq() const
    {
        return ledgerSeq_;
    }

    /** Return the raw integer representation of the transaction result code.
     *
     *  Holds 255 until `addRaw()` has been called; callers that need a typed
     *  result should prefer `getResultTER()`.
     *
     *  @return Raw `TER` integer value (0 = tesSUCCESS, 101–255 = tec codes).
     */
    [[nodiscard]] int
    getResult() const
    {
        return result_;
    }

    /** Return the transaction result as a typed `TER` code.
     *
     *  @note Returns a meaningless value if called before `addRaw()`.
     */
    [[nodiscard]] TER
    getResultTER() const
    {
        return TER::fromInt(result_);
    }

    /** Return the transaction's ordinal position within its ledger.
     *
     *  Holds `UINT32_MAX` until `addRaw()` has been called.
     */
    [[nodiscard]] std::uint32_t
    getIndex() const
    {
        return index_;
    }

    /** Register or reclassify a ledger entry in the affected-nodes list.
     *
     *  If a node with the given key already exists, its field name and
     *  `sfLedgerEntryType` are updated in place — allowing a caller to
     *  upgrade an entry from `sfModifiedNode` to `sfDeletedNode`, for
     *  example. Otherwise a new entry is appended to `nodes_`.
     *
     *  @param node     The `sfLedgerIndex` key of the affected ledger entry.
     *  @param type     Node category: `sfCreatedNode`, `sfModifiedNode`, or
     *      `sfDeletedNode`.
     *  @param nodeType The `sfLedgerEntryType` value (e.g. `ltACCOUNT_ROOT`).
     */
    void
    setAffectedNode(uint256 const&, SField const& type, std::uint16_t nodeType);

    /** Retrieve or create the metadata node for a live ledger entry.
     *
     *  Finds the node whose `sfLedgerIndex` matches `node->key()`, creating a
     *  new entry (copying the entry type from the live SLE) if none exists.
     *  Used by `ApplyStateTable` to attach `sfPreviousFields`, `sfFinalFields`,
     *  or `sfNewFields` sub-objects after the node has been registered via
     *  `setAffectedNode`.
     *
     *  @param node Live ledger entry whose metadata node is requested.
     *  @param type Node category used when a new entry must be created.
     *  @return Mutable reference to the matching `STObject` in `nodes_`.
     */
    STObject&
    getAffectedNode(SLE::ref node, SField const& type);  // create if needed

    /** Retrieve the metadata node for a ledger-entry key, asserting it exists.
     *
     *  Used after `setAffectedNode` has guaranteed registration of the key.
     *  Calls `UNREACHABLE` in debug builds and throws in release if the key is
     *  absent — absence indicates a programming error, not a data error.
     *
     *  @param node The `sfLedgerIndex` key to look up.
     *  @return Mutable reference to the matching `STObject` in `nodes_`.
     *  @throws std::runtime_error If the node is not found (should never occur
     *      in correct callers).
     */
    STObject&
    getAffectedNode(uint256 const&);

    /** Return the set of every account implicated by this transaction's metadata.
     *
     *  Scans `sfNewFields` for created nodes and `sfFinalFields` for modified
     *  and deleted nodes, extracting `AccountID` values from `STAccount`
     *  fields, from the issuers embedded in `sfLowLimit`, `sfHighLimit`,
     *  `sfTakerPays`, and `sfTakerGets` amounts, and from the issuer encoded in
     *  `sfMPTokenIssuanceID`.
     *
     *  @note The behavior must remain identical to that of the JavaScript
     *      `Meta#getAffectedAccounts` method for cross-platform consistency.
     *  @return Sorted, deduplicated flat set of affected `AccountID` values.
     */
    [[nodiscard]] boost::container::flat_set<AccountID>
    getAffectedAccounts() const;

    /** Serialize the metadata as JSON via the underlying `STObject`.
     *
     *  Delegates to `getAsObject().getJson(p)`. Must not be called before
     *  `addRaw()` finalizes `result_`.
     *
     *  @param p JSON formatting options.
     *  @return JSON representation of the complete metadata object.
     */
    [[nodiscard]] json::Value
    getJson(JsonOptions p) const
    {
        return getAsObject().getJson(p);
    }

    /** Finalize and append the metadata bytes to a Serializer.
     *
     *  Records `result` and `index`, then sorts `nodes_` by `sfLedgerIndex`
     *  before calling `getAsObject().add(s)`. The sort is consensus-critical:
     *  nodes are accumulated in execution order, which is not deterministic
     *  across implementations; sorting by key ensures all validators produce
     *  byte-identical metadata blobs for the same transaction.
     *
     *  Must be called exactly once per `TxMeta` instance, before any call to
     *  `getAsObject()` or `getJson()`.
     *
     *  @param s      Serializer to append the metadata bytes to.
     *  @param result The `TER` outcome of the transaction.
     *  @param index  The transaction's ordinal position within the closed ledger.
     */
    void
    addRaw(Serializer&, TER, std::uint32_t index);

    /** Assemble the complete metadata as an `STObject`.
     *
     *  Constructs an `sfTransactionMetaData` object containing
     *  `sfTransactionResult`, `sfTransactionIndex`, `sfAffectedNodes`, and
     *  optionally `sfDeliveredAmount` and `sfParentBatchID`.
     *
     *  @note Asserts `result_ != 255`; call `addRaw()` first.
     *  @return Fully populated `STObject` tagged `sfTransactionMetaData`.
     */
    [[nodiscard]] STObject
    getAsObject() const;

    /** Return a mutable reference to the raw affected-nodes array. */
    STArray&
    getNodes()
    {
        return nodes_;
    }

    /** Return a read-only reference to the raw affected-nodes array. */
    [[nodiscard]] STArray const&
    getNodes() const
    {
        return nodes_;
    }

    /** Extract and store optional fields from an existing metadata object.
     *
     *  Reads `sfDeliveredAmount` and `sfParentBatchID` from `obj` when
     *  present. Called by the deserializing constructors immediately after
     *  extracting the required fields.
     *
     *  @param obj Source object from which to read optional metadata fields.
     */
    void
    setAdditionalFields(STObject const& obj)
    {
        if (obj.isFieldPresent(sfDeliveredAmount))
            deliveredAmount_ = obj.getFieldAmount(sfDeliveredAmount);

        if (obj.isFieldPresent(sfParentBatchID))
            parentBatchID_ = obj.getFieldH256(sfParentBatchID);
    }

    /** Return the optional delivered amount recorded for this transaction.
     *
     *  Present only for payment transactions where the amount actually
     *  delivered may differ from the `Amount` field (e.g. partial payments).
     *  Absent for all other transaction types.
     */
    [[nodiscard]] std::optional<STAmount> const&
    getDeliveredAmount() const
    {
        return deliveredAmount_;
    }

    /** Set the optional delivered amount for a payment transaction.
     *
     *  Called by `ApplyStateTable` after the payment engine resolves path
     *  execution. Pass `std::nullopt` to clear a previously set value.
     *
     *  @param amount The actual amount delivered, or `std::nullopt` to clear.
     */
    void
    setDeliveredAmount(std::optional<STAmount> const& amount)
    {
        deliveredAmount_ = amount;
    }

    /** Set the optional parent Batch transaction ID.
     *
     *  Links this transaction's metadata to the enclosing Batch transaction
     *  (Batch amendment). Propagated from `ApplyContext`, which receives it
     *  from the outermost batch-processing layer.
     *
     *  @param id The hash of the parent Batch transaction, or `std::nullopt`
     *      if this transaction is not part of a batch.
     */
    void
    setParentBatchID(std::optional<uint256> const& id)
    {
        parentBatchID_ = id;
    }

private:
    uint256 transactionID_;
    std::uint32_t ledgerSeq_;
    std::uint32_t index_;
    int result_;

    std::optional<STAmount> deliveredAmount_;
    std::optional<uint256> parentBatchID_;

    STArray nodes_;
};

}  // namespace xrpl
