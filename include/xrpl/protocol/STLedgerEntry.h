#pragma once

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STObject.h>

namespace xrpl {

class Rules;
namespace test {
class Invariants_test;
}  // namespace test

/** The C++ representation of a single object in the XRPL ledger state (universally aliased as `SLE`).
 *
 *  Each ledger entry lives in a `SHAMap` keyed by a 256-bit `key_`. The
 *  `type_` member names what the object is (account root, offer, escrow,
 *  trust line, etc.) and determines which `SOTemplate` governs its field
 *  layout. Construction from a `Keylet` looks up the registered
 *  `LedgerFormats` schema and throws immediately if the type is unknown,
 *  ensuring that an SLE always has a valid, self-consistent type.
 *
 *  Declared `final`: ledger-entry type diversity is handled entirely through
 *  the `LedgerFormats` registration system at runtime, not through C++
 *  subclass hierarchies.
 *
 *  @see SLE (alias below), Keylet, LedgerFormats
 */
class STLedgerEntry final : public STObject, public CountedObject<STLedgerEntry>
{
    uint256 key_;
    LedgerEntryType type_;

public:
    /** Shared-pointer to a mutable ledger entry. */
    using pointer = std::shared_ptr<STLedgerEntry>;
    /** Const reference to a shared-pointer to a mutable ledger entry. */
    using ref = std::shared_ptr<STLedgerEntry> const&;
    /** Shared-pointer to an immutable ledger entry. */
    using const_pointer = std::shared_ptr<STLedgerEntry const>;
    /** Const reference to a shared-pointer to an immutable ledger entry. */
    using const_ref = std::shared_ptr<STLedgerEntry const> const&;

    /** Construct a new, empty ledger entry for the given keylet.
     *
     *  Looks up the `LedgerFormats` schema for `k.type`, applies the
     *  `SOTemplate` (populating all declared fields at their default values),
     *  and writes `sfLedgerEntryType` so the wire encoding is self-describing.
     *
     *  @param k Keylet carrying the SHAMap key and the desired entry type.
     *  @throws std::runtime_error if `k.type` is not registered in
     *      `LedgerFormats`.
     */
    explicit STLedgerEntry(Keylet const& k);

    /** Convenience constructor that delegates to the `Keylet` path.
     *
     *  Equivalent to `STLedgerEntry(Keylet(type, key))`. Prefer the
     *  `Keylet` overload where one is already available.
     *
     *  @param type The ledger entry type.
     *  @param key  SHAMap key for this entry.
     *  @throws std::runtime_error if `type` is not registered in
     *      `LedgerFormats`.
     */
    STLedgerEntry(LedgerEntryType type, uint256 const& key);

    /** Deserialize a ledger entry from a byte stream.
     *
     *  Reads all fields from `sit` into the underlying `STObject`, then
     *  resolves `sfLedgerEntryType` to set `type_` and enforces the matching
     *  `SOTemplate` via `setSLEType()`.
     *
     *  @param sit   Wire-format byte cursor; consumed in place.
     *  @param index SHAMap key that addresses this entry in the ledger state.
     *  @throws std::runtime_error if the deserialized type is unrecognized or
     *      the field set does not conform to the declared template.
     */
    STLedgerEntry(SerialIter& sit, uint256 const& index);

    /** Convenience rvalue overload that forwards to the lvalue `SerialIter` constructor.
     *
     *  `SerialIter` is consumed by position rather than by move semantics, so
     *  this overload simply binds the rvalue to an lvalue reference and
     *  delegates.
     *
     *  @param sit   Wire-format byte cursor; consumed in place.
     *  @param index SHAMap key that addresses this entry in the ledger state.
     *  @throws std::runtime_error if the deserialized type is unrecognized or
     *      the field set does not conform to the declared template.
     */
    STLedgerEntry(SerialIter&& sit, uint256 const& index);

    /** Promote a pre-populated `STObject` to a typed ledger entry.
     *
     *  Used when fields have already been parsed into a generic `STObject`
     *  and need to be re-interpreted with a concrete `LedgerEntryType`.
     *  Delegates to `setSLEType()` for type resolution and template
     *  conformance.
     *
     *  @param object The source object, copied into this entry.
     *  @param index  SHAMap key that addresses this entry in the ledger state.
     *  @throws std::runtime_error if `sfLedgerEntryType` is absent,
     *      unrecognized, or the field set does not conform to the declared
     *      template.
     */
    STLedgerEntry(STObject const& object, uint256 const& index);

    /** Return the serialized type identifier for ledger entries (`STI_LEDGERENTRY`). */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return a verbose diagnostic string containing the type name, key, and all field values.
     *
     *  Re-validates `type_` against `LedgerFormats` as a defensive invariant
     *  check before emitting output.
     *
     *  @throws std::runtime_error if `type_` is no longer recognized
     *      (indicates in-memory corruption).
     */
    [[nodiscard]] std::string
    getFullText() const override;

    /** Return a compact diagnostic string containing the hex key and field contents. */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize this entry to JSON, augmenting the base `STObject` output.
     *
     *  Injects `"index"` (the hex-encoded SHAMap key) because `key_` is not
     *  stored as a serialized field. For `ltMPTOKEN_ISSUANCE` objects, also
     *  injects `"mpt_issuance_id"` computed from `sfSequence` and `sfIssuer`
     *  — this derived identifier is not stored on-ledger and is recomputed
     *  on every read to keep consensus-critical storage non-redundant.
     *
     *  @param options Controls JSON formatting (e.g., binary vs. human-readable).
     *  @return A `Json::Value` object with all fields plus the injected keys.
     */
    [[nodiscard]] json::Value
    getJson(JsonOptions options = JsonOptions::Values::None) const override;

    /** Return the 256-bit SHAMap key that locates this entry in the ledger state. */
    [[nodiscard]] uint256 const&
    key() const;

    /** Return the `LedgerEntryType` that identifies what kind of object this is. */
    [[nodiscard]] LedgerEntryType
    getType() const;

    /** Determine whether this entry participates in transaction threading.
     *
     *  Threading links each ledger entry to the transaction that last modified
     *  it via `sfPreviousTxnID` / `sfPreviousTxnLgrSeq`. Five types
     *  (`ltDIR_NODE`, `ltAMENDMENTS`, `ltFEE_SETTINGS`, `ltNEGATIVE_UNL`,
     *  `ltAMM`) only gained `sfPreviousTxnID` support when the
     *  `fixPreviousTxnID` amendment activated; before that, this method
     *  returns `false` for those types even if the field technically exists in
     *  the template, preventing premature use of threading on objects that
     *  historically lacked it.
     *
     *  @param rules The active amendment rules for the current ledger.
     *  @return `true` if this entry carries `sfPreviousTxnID` and threading
     *      is permitted under the current rules.
     */
    [[nodiscard]] bool
    isThreadedType(Rules const& rules) const;

    /** Update the threading fields to record that `txID` last modified this entry.
     *
     *  Reads the current `sfPreviousTxnID`; if it already equals `txID`, the
     *  transaction has already been applied to this entry — asserts that
     *  `sfPreviousTxnLgrSeq` also matches and returns `false` (idempotency
     *  guard against double-application). Otherwise writes `txID` and
     *  `ledgerSeq` into the object and captures the old values in the output
     *  parameters so callers can reconstruct the modification chain.
     *
     *  @param txID         Hash of the transaction that is modifying this entry.
     *  @param ledgerSeq    Sequence number of the ledger containing `txID`.
     *  @param prevTxID     [out] The previous value of `sfPreviousTxnID`.
     *  @param prevLedgerID [out] The previous value of `sfPreviousTxnLgrSeq`.
     *  @return `true` if the fields were updated; `false` if `txID` was
     *      already threaded and the entry is unchanged.
     */
    bool
    thread(
        uint256 const& txID,
        std::uint32_t ledgerSeq,
        uint256& prevTxID,
        std::uint32_t& prevLedgerID);

private:
    /** Resolve `type_` from the embedded `sfLedgerEntryType` field and enforce
     *  template conformance on an already-populated object.
     *
     *  Post-hoc counterpart to the `set(SOTemplate)` call in the `Keylet`
     *  constructor: instead of initializing an empty object, it validates
     *  and conforms an already-populated one. Called after wire
     *  deserialization and after `STObject` promotion.
     *
     *  @throws std::runtime_error if `sfLedgerEntryType` names an unrecognized
     *      type, or if `applyTemplate()` rejects the field set.
     */
    void
    setSLEType();

    friend test::Invariants_test;  // this test wants access to the private
                                   // type_

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

/** Canonical short alias for `STLedgerEntry`, used pervasively throughout the codebase. */
using SLE = STLedgerEntry;

inline STLedgerEntry::STLedgerEntry(LedgerEntryType type, uint256 const& key)
    : STLedgerEntry(Keylet(type, key))
{
}

inline STLedgerEntry::STLedgerEntry(
    SerialIter&& sit,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    uint256 const& index)
    : STLedgerEntry(sit, index)
{
}

inline uint256 const&
STLedgerEntry::key() const
{
    return key_;
}

inline LedgerEntryType
STLedgerEntry::getType() const
{
    return type_;
}

}  // namespace xrpl
