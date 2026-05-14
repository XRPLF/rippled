/** @file
 *  Implementation of `STLedgerEntry`, the typed representation of a single
 *  object in the XRPL ledger state.
 *
 *  Covers construction from a `Keylet` (new entry), deserialization from a
 *  `SerialIter` (wire bytes), and promotion from a generic `STObject`. Also
 *  implements JSON representation (including the synthetic `mpt_issuance_id`
 *  for `ltMPTOKEN_ISSUANCE` objects) and the transaction-threading mechanism
 *  (`sfPreviousTxnID` / `sfPreviousTxnLgrSeq`) that links each ledger entry
 *  to the transaction that last modified it.
 */
#include <xrpl/protocol/STLedgerEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <boost/format/free_funcs.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Construct a new, empty ledger entry for the given keylet.
 *
 *  Looks up the `LedgerFormats` schema for `k.type`, applies the
 *  `SOTemplate` (populating all declared fields at their default values),
 *  and writes `sfLedgerEntryType` so the wire encoding is self-describing.
 *
 *  @throws std::runtime_error if `k.type` is not registered in
 *      `LedgerFormats`.
 */
STLedgerEntry::STLedgerEntry(Keylet const& k) : STObject(sfLedgerEntry), key_(k.key), type_(k.type)
{
    auto const format = LedgerFormats::getInstance().findByType(type_);

    if (format == nullptr)
    {
        Throw<std::runtime_error>(
            "Attempt to create a SLE of unknown type " +
            std::to_string(safeCast<std::uint16_t>(k.type)));
    }

    set(format->getSOTemplate());

    setFieldU16(sfLedgerEntryType, static_cast<std::uint16_t>(type_));
}

/** Deserialize a ledger entry from a byte stream.
 *
 *  Reads all fields from `sit` into the underlying `STObject`, then calls
 *  `setSLEType()` to resolve the `sfLedgerEntryType` field, apply the
 *  matching `SOTemplate`, and validate field conformance.
 *
 *  @param sit   Wire-format byte cursor; consumed in place.
 *  @param index SHAMap key that addresses this entry in the ledger state.
 *  @throws std::runtime_error if the deserialized type is unrecognized or
 *      the field set does not conform to the declared template.
 */
STLedgerEntry::STLedgerEntry(SerialIter& sit, uint256 const& index)
    : STObject(sfLedgerEntry), key_(index), type_(ltANY)
{
    set(sit);
    setSLEType();
}

/** Promote a pre-populated `STObject` to a typed ledger entry.
 *
 *  Used when fields have already been parsed into a generic `STObject` and
 *  need to be re-interpreted with a concrete `LedgerEntryType`. Delegates
 *  to `setSLEType()` for type resolution and template conformance.
 *
 *  @param object The source object, copied into this entry.
 *  @param index  SHAMap key that addresses this entry in the ledger state.
 *  @throws std::runtime_error if `sfLedgerEntryType` is absent, unrecognized,
 *      or the field set does not conform to the declared template.
 */
STLedgerEntry::STLedgerEntry(STObject const& object, uint256 const& index)
    : STObject(object), key_(index), type_(ltANY)
{
    setSLEType();
}

/** Resolve `type_` from the embedded `sfLedgerEntryType` field and enforce
 *  template conformance on an already-populated object.
 *
 *  This is the post-hoc counterpart to the `set(SOTemplate)` call in the
 *  `Keylet` constructor: instead of initializing an empty object, it
 *  validates and conforms an already-populated one. Called after wire
 *  deserialization and after `STObject` promotion.
 *
 *  @throws std::runtime_error if `sfLedgerEntryType` names an unrecognized
 *      type, or if `applyTemplate()` rejects the field set.
 */
void
STLedgerEntry::setSLEType()
{
    auto format = LedgerFormats::getInstance().findByType(
        safeCast<LedgerEntryType>(getFieldU16(sfLedgerEntryType)));

    if (format == nullptr)
        Throw<std::runtime_error>("invalid ledger entry type");

    type_ = format->getType();
    applyTemplate(format->getSOTemplate());  // May throw
}

/** Return a verbose diagnostic string including the type name, key, and all
 *  field values.
 *
 *  Re-validates `type_` against `LedgerFormats` as a defensive invariant
 *  check before emitting output.
 *
 *  @throws std::runtime_error if `type_` is no longer recognized (indicates
 *      in-memory corruption).
 */
std::string
STLedgerEntry::getFullText() const
{
    auto const format = LedgerFormats::getInstance().findByType(type_);

    if (format == nullptr)
        Throw<std::runtime_error>("invalid ledger entry type");

    std::string ret = "\"";
    ret += to_string(key_);
    ret += "\" = { ";
    ret += format->getName();
    ret += ", ";
    ret += STObject::getFullText();
    ret += "}";
    return ret;
}

/** Placement-copy this entry into `buf` for `detail::STVar` small-buffer
 *  optimization.
 *
 *  @param n   Size of the destination buffer in bytes.
 *  @param buf Pointer to the destination buffer.
 *  @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STLedgerEntry::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Placement-move this entry into `buf` for `detail::STVar` small-buffer
 *  optimization.
 *
 *  @param n   Size of the destination buffer in bytes.
 *  @param buf Pointer to the destination buffer.
 *  @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STLedgerEntry::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Return the serialized type identifier for ledger entries (`STI_LEDGERENTRY`). */
SerializedTypeID
STLedgerEntry::getSType() const
{
    return STI_LEDGERENTRY;
}

/** Return a compact diagnostic string containing the hex key and field
 *  contents.
 */
std::string
STLedgerEntry::getText() const
{
    return str(boost::format("{ %s, %s }") % to_string(key_) % STObject::getText());
}

/** Serialize this entry to JSON, augmenting the base `STObject` output.
 *
 *  Injects `"index"` (the hex-encoded SHAMap key) because `key_` is not
 *  stored as a serialized field. For `ltMPTOKEN_ISSUANCE` objects, also
 *  injects `"mpt_issuance_id"` computed from `sfSequence` and `sfIssuer`
 *  — this derived identifier is not stored on-ledger; it is recomputed
 *  on every read to keep consensus-critical storage non-redundant.
 *
 *  @param options Controls JSON formatting (e.g., binary vs. human-readable).
 *  @return A `Json::Value` object with all fields plus the injected keys.
 */
json::Value
STLedgerEntry::getJson(JsonOptions options) const
{
    json::Value ret(STObject::getJson(options));

    ret[jss::index] = to_string(key_);

    if (getType() == ltMPTOKEN_ISSUANCE)
    {
        ret[jss::mpt_issuance_id] =
            to_string(makeMptID(getFieldU32(sfSequence), getAccountID(sfIssuer)));
    }

    return ret;
}

/** Determine whether this entry participates in transaction threading.
 *
 *  Threading links each ledger entry to the transaction that last modified
 *  it via `sfPreviousTxnID` / `sfPreviousTxnLgrSeq`. Five types
 *  (`ltDIR_NODE`, `ltAMENDMENTS`, `ltFEE_SETTINGS`, `ltNEGATIVE_UNL`,
 *  `ltAMM`) only gained `sfPreviousTxnID` support when the
 *  `fixPreviousTxnID` amendment activated; before that, this method
 *  returns `false` for those types to avoid producing threading fields
 *  that pre-amendment validators would not expect.
 *
 *  @param rules The active amendment rules for the current ledger.
 *  @return `true` if this entry carries `sfPreviousTxnID` and threading
 *      is permitted under the current rules.
 *  @note The amendment guard performs a linear scan over a five-element
 *      compile-time array — acceptable cost for transaction-application
 *      frequency.
 */
bool
STLedgerEntry::isThreadedType(Rules const& rules) const
{
    static constexpr std::array<LedgerEntryType, 5> kNEW_PREVIOUS_TXN_ID_TYPES = {
        ltDIR_NODE, ltAMENDMENTS, ltFEE_SETTINGS, ltNEGATIVE_UNL, ltAMM};
    // Exclude PrevTxnID/PrevTxnLgrSeq if the fixPreviousTxnID amendment is not
    // enabled and the ledger object type is in the above set
    bool const excludePrevTxnID = !rules.enabled(fixPreviousTxnID) &&
        (std::count(
             kNEW_PREVIOUS_TXN_ID_TYPES.cbegin(), kNEW_PREVIOUS_TXN_ID_TYPES.cend(), type_) != 0);
    return !excludePrevTxnID && getFieldIndex(sfPreviousTxnID) != -1;
}

/** Update the threading fields to record that `txID` last modified this
 *  entry, and capture the previous transaction link for metadata.
 *
 *  Reads `sfPreviousTxnID`; if it already equals `txID`, the transaction
 *  has been applied before — asserts that `sfPreviousTxnLgrSeq` also
 *  matches and returns `false` to signal no-op (guards against
 *  double-application). Otherwise writes the new `txID` and `ledgerSeq`
 *  and captures the old values in the output parameters so that callers
 *  can reconstruct the modification chain.
 *
 *  @param txID        Hash of the transaction that is modifying this entry.
 *  @param ledgerSeq   Sequence number of the ledger containing `txID`.
 *  @param prevTxID    [out] The previous value of `sfPreviousTxnID`.
 *  @param prevLedgerID [out] The previous value of `sfPreviousTxnLgrSeq`.
 *  @return `true` if the fields were updated; `false` if `txID` was
 *      already threaded (entry unchanged).
 */
bool
STLedgerEntry::thread(
    uint256 const& txID,
    std::uint32_t ledgerSeq,
    uint256& prevTxID,
    std::uint32_t& prevLedgerID)
{
    uint256 const oldPrevTxID = getFieldH256(sfPreviousTxnID);

    JLOG(debugLog().info()) << "Thread Tx:" << txID << " prev:" << oldPrevTxID;

    if (oldPrevTxID == txID)
    {
        // this transaction is already threaded
        XRPL_ASSERT(
            getFieldU32(sfPreviousTxnLgrSeq) == ledgerSeq,
            "xrpl::STLedgerEntry::thread : ledger sequence match");
        return false;
    }

    prevTxID = oldPrevTxID;
    prevLedgerID = getFieldU32(sfPreviousTxnLgrSeq);
    setFieldH256(sfPreviousTxnID, txID);
    setFieldU32(sfPreviousTxnLgrSeq, ledgerSeq);
    return true;
}

}  // namespace xrpl
