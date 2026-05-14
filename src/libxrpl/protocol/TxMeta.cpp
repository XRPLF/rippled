/** @file
 *  Implementation of TxMeta: construction, node accumulation, account
 *  extraction, and deterministic serialization of per-transaction metadata.
 *
 *  @see TxMeta.h for the public interface.
 *  @see ApplyStateTable.cpp for the primary caller that drives accumulation.
 */
#include <xrpl/protocol/TxMeta.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace xrpl {

/** Construct from an already-parsed STObject (e.g. during ledger replay).
 *
 *  The member initializer list seeds `nodes_` via `getFieldArray`, but the
 *  constructor body immediately overwrites it using `peekAtPField` + a
 *  `dynamic_cast`. The redundant first extraction is a mild inefficiency
 *  retained from earlier refactoring; the authoritative assignment is the
 *  body `nodes_ = *affectedNodes`. The assertion guards against corrupted
 *  or malformed ledger data where the runtime type does not match.
 */
TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, STObject const& obj)
    : transactionID_(txid), ledgerSeq_(ledger), nodes_(obj.getFieldArray(sfAffectedNodes))
{
    result_ = obj.getFieldU8(sfTransactionResult);
    index_ = obj.getFieldU32(sfTransactionIndex);

    auto affectedNodes = dynamic_cast<STArray const*>(obj.peekAtPField(sfAffectedNodes));
    XRPL_ASSERT(affectedNodes, "xrpl::TxMeta::TxMeta(STObject) : type cast succeeded");
    if (affectedNodes != nullptr)
        nodes_ = *affectedNodes;

    setAdditionalFields(obj);
}

/** Deserialize metadata from a raw byte blob (e.g. loaded from the ledger
 *  database or received over the wire).
 *
 *  Wraps `vec` in a `SerialIter`, materializes a full `STObject` tagged
 *  `sfMetadata`, then extracts the three required fields. `nodes_` is
 *  pre-constructed with a capacity of 32 to match the reservation used in
 *  the empty constructor; the actual contents are populated by
 *  `getFieldArray`.
 */
TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, Blob const& vec)
    : transactionID_(txid), ledgerSeq_(ledger), nodes_(sfAffectedNodes, 32)
{
    SerialIter sit(makeSlice(vec));

    STObject const obj(sit, sfMetadata);
    result_ = obj.getFieldU8(sfTransactionResult);
    index_ = obj.getFieldU32(sfTransactionIndex);
    nodes_ = obj.getFieldArray(sfAffectedNodes);

    setAdditionalFields(obj);
}

/** Construct an empty metadata object for a transaction being applied.
 *
 *  `result_` is initialized to 255 and `index_` to `UINT32_MAX` as
 *  sentinels marking the object as not yet finalized. `getAsObject()`
 *  asserts `result_ != 255`; callers must invoke `addRaw()` before
 *  serializing. The `nodes_` array pre-reserves 32 slots to avoid
 *  reallocation during accumulation for all but the most complex
 *  transactions.
 */
TxMeta::TxMeta(uint256 const& transactionID, std::uint32_t ledger)
    : transactionID_(transactionID)
    , ledgerSeq_(ledger)
    , index_(std::numeric_limits<std::uint32_t>::max())
    , result_(255)
    , nodes_(sfAffectedNodes)
{
    nodes_.reserve(32);
}

/** Register or update a ledger entry in the affected-nodes list.
 *
 *  If an entry with the same `sfLedgerIndex` already exists, its `type`
 *  and `sfLedgerEntryType` fields are updated in place. Otherwise a new
 *  `STObject` is appended to `nodes_`. The linear scan is deliberate:
 *  the affected-node count per transaction is small (typically well under
 *  32) and a map would add overhead without benefit.
 *
 *  @param node   The ledger-entry key (`sfLedgerIndex`) being affected.
 *  @param type   The node category — `sfCreatedNode`, `sfModifiedNode`, or
 *      `sfDeletedNode`.
 *  @param nodeType The `sfLedgerEntryType` value (e.g. `ltACCOUNT_ROOT`).
 */
void
TxMeta::setAffectedNode(uint256 const& node, SField const& type, std::uint16_t nodeType)
{
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
        {
            n.setFName(type);
            n.setFieldU16(sfLedgerEntryType, nodeType);
            return;
        }
    }

    nodes_.pushBack(STObject(type));
    STObject& obj = nodes_.back();

    XRPL_ASSERT(obj.getFName() == type, "xrpl::TxMeta::setAffectedNode : field type match");
    obj.setFieldH256(sfLedgerIndex, node);
    obj.setFieldU16(sfLedgerEntryType, nodeType);
}

/** Collect all AccountIDs whose ledger state was touched by this transaction.
 *
 *  Scans `sfNewFields` for `sfCreatedNode` entries and `sfFinalFields` for
 *  modified and deleted nodes — the asymmetry reflects the metadata schema,
 *  where newly created objects record state in `sfNewFields` while
 *  modified/deleted objects record their last-known state in `sfFinalFields`.
 *
 *  Three distinct field shapes are handled:
 *   - `STAccount` fields — account IDs stored directly (e.g. `sfAccount`,
 *     `sfDestination`).
 *   - `STAmount` fields `sfLowLimit`, `sfHighLimit`, `sfTakerPays`,
 *     `sfTakerGets` — trust-line and order-book amounts that embed an
 *     issuer `AccountID`.
 *   - `sfMPTokenIssuanceID` — a 192-bit `STBitString` from which the issuer
 *     `AccountID` is recovered via `MPTIssue`.
 *
 *  @note The behavior of this method must remain identical to that of the
 *      JavaScript `Meta#getAffectedAccounts` method to preserve
 *      cross-platform consistency.
 *  @return Sorted, deduplicated set of affected `AccountID` values.
 */
boost::container::flat_set<AccountID>
TxMeta::getAffectedAccounts() const
{
    boost::container::flat_set<AccountID> list;
    list.reserve(10);

    for (auto const& node : nodes_)
    {
        int const index =
            node.getFieldIndex((node.getFName() == sfCreatedNode) ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            auto const* inner = dynamic_cast<STObject const*>(&node.peekAtIndex(index));
            XRPL_ASSERT(inner, "xrpl::getAffectedAccounts : STObject type cast succeeded");
            if (inner != nullptr)
            {
                for (auto const& field : *inner)
                {
                    if (auto sa = dynamic_cast<STAccount const*>(&field))
                    {
                        XRPL_ASSERT(!sa->isDefault(), "xrpl::getAffectedAccounts : account is set");
                        if (!sa->isDefault())
                            list.insert(sa->value());
                    }
                    else if (
                        (field.getFName() == sfLowLimit) || (field.getFName() == sfHighLimit) ||
                        (field.getFName() == sfTakerPays) || (field.getFName() == sfTakerGets))
                    {
                        auto lim = dynamic_cast<STAmount const*>(&field);
                        XRPL_ASSERT(
                            lim,
                            "xrpl::getAffectedAccounts : STAmount type cast "
                            "succeeded");

                        if (lim != nullptr)
                        {
                            auto issuer = lim->getIssuer();

                            if (issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                    else if (field.getFName() == sfMPTokenIssuanceID)
                    {
                        auto mptID = dynamic_cast<STBitString<192> const*>(&field);
                        if (mptID != nullptr)
                        {
                            auto issuer = MPTIssue(mptID->value()).getIssuer();

                            if (issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                }
            }
        }
    }

    return list;
}

/** Retrieve the metadata node for a ledger entry, creating it if absent.
 *
 *  Used by `ApplyStateTable` in the second accumulation phase, after
 *  `setAffectedNode` has registered the entry, to attach `sfPreviousFields`,
 *  `sfFinalFields`, or `sfNewFields` sub-objects describing field-level
 *  deltas. Unlike the `uint256` overload this variant silently creates a new
 *  entry when the node has not been previously registered.
 *
 *  @param node The ledger entry whose metadata node to retrieve or create.
 *  @param type The node category field (`sfCreatedNode`, `sfModifiedNode`,
 *      or `sfDeletedNode`) used when creating a new entry.
 *  @return Reference to the `STObject` node in `nodes_`.
 */
STObject&
TxMeta::getAffectedNode(SLE::ref node, SField const& type)
{
    uint256 const index = node->key();
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == index)
            return n;
    }
    nodes_.pushBack(STObject(type));
    STObject& obj = nodes_.back();

    XRPL_ASSERT(
        obj.getFName() == type, "xrpl::TxMeta::getAffectedNode(SLE::ref) : field type match");
    obj.setFieldH256(sfLedgerIndex, index);
    obj.setFieldU16(sfLedgerEntryType, node->getFieldU16(sfLedgerEntryType));

    return obj;
}

/** Retrieve the metadata node for a ledger-entry key, asserting it exists.
 *
 *  Internal variant called only after `setAffectedNode` has guaranteed
 *  registration of the given key. If the node is absent the call is a
 *  programming error: `UNREACHABLE` fires in debug builds and
 *  `std::runtime_error` is thrown as a last-resort guard in release.
 *
 *  @param node The `sfLedgerIndex` key to look up.
 *  @return Reference to the matching `STObject` node in `nodes_`.
 *  @throws std::runtime_error If the node is not present (should never
 *      occur in correct callers; the error path is excluded from coverage).
 */
STObject&
TxMeta::getAffectedNode(uint256 const& node)
{
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
            return n;
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::TxMeta::getAffectedNode(uint256) : node not found");
    Throw<std::runtime_error>("Affected node not found");
    return *(nodes_.begin());  // Silence compiler warning.
    // LCOV_EXCL_STOP
}

/** Assemble the complete metadata as an STObject.
 *
 *  Includes the three required fields (`sfTransactionResult`,
 *  `sfTransactionIndex`, `sfAffectedNodes`) and, when present, the optional
 *  `sfDeliveredAmount` and `sfParentBatchID` fields.
 *
 *  @note Asserts `result_ != 255` to catch calls made before `addRaw()`
 *      has finalized the result code. Emitting metadata with the sentinel
 *      value would silently corrupt the ledger record.
 *  @return A fully populated `STObject` tagged `sfTransactionMetaData`.
 */
STObject
TxMeta::getAsObject() const
{
    STObject metaData(sfTransactionMetaData);
    XRPL_ASSERT(result_ != 255, "xrpl::TxMeta::getAsObject : result_ is set");
    metaData.setFieldU8(sfTransactionResult, result_);
    metaData.setFieldU32(sfTransactionIndex, index_);
    metaData.emplaceBack(nodes_);
    if (deliveredAmount_.has_value())
        metaData.setFieldAmount(sfDeliveredAmount, *deliveredAmount_);

    if (parentBatchID_.has_value())
        metaData.setFieldH256(sfParentBatchID, *parentBatchID_);

    return metaData;
}

/** Finalize and serialize metadata into a `Serializer`.
 *
 *  Stamps `result_` and `index_` with the actual `TER` outcome and ledger
 *  position, then sorts `nodes_` by `sfLedgerIndex` before delegating to
 *  `getAsObject().add(s)`. The sort is critical for consensus: two
 *  validators applying the same transaction must produce byte-identical
 *  metadata blobs, and map-iteration order over `ApplyStateTable::items_`
 *  is not guaranteed stable across implementations.
 *
 *  @param s      Serializer to append the metadata bytes to.
 *  @param result The `TER` outcome of the transaction.
 *  @param index  The transaction's position within the closed ledger
 *      (from `OpenView::txCount()`).
 */
void
TxMeta::addRaw(Serializer& s, TER result, std::uint32_t index)
{
    result_ = TERtoInt(result);
    index_ = index;
    XRPL_ASSERT(
        (result_ == 0) || ((result_ > 100) && (result_ <= 255)),
        "xrpl::TxMeta::addRaw : valid TER input");

    nodes_.sort([](STObject const& o1, STObject const& o2) {
        return o1.getFieldH256(sfLedgerIndex) < o2.getFieldH256(sfLedgerIndex);
    });

    getAsObject().add(s);
}

}  // namespace xrpl
