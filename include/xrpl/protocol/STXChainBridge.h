#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STIssue.h>

namespace xrpl {

class Serializer;
class STObject;

/** Serialized type encoding the four-field specification of an XRPL cross-chain bridge.
 *
 *  A bridge connects two independent ledgers: a *locking chain* (where XRP or
 *  tokens are held in escrow) and an *issuing chain* (where a wrapped
 *  representation is minted). Each side is described by a door account
 *  (`AccountID`) and an asset (`Issue`). This class bundles those four pieces
 *  — `LockingChainDoor`, `LockingChainIssue`, `IssuingChainDoor`,
 *  `IssuingChainIssue` — into a single, typed, wire-format ledger field that
 *  appears in bridge-related transactions and ledger entries.
 *
 *  Inherits `STBase` (type-ID `STI_XCHAIN_BRIDGE`) for polymorphic
 *  serialization and `CountedObject` for debug instance tracking.
 *
 *  Both `operator==` and `operator<` compare all four fields in declaration
 *  order via `std::tie`, making `STXChainBridge` usable as a key in ordered
 *  associative containers.
 *
 *  @see XChainAttestations.h for how bridges are consumed by witness and
 *      attestation logic.
 */
class STXChainBridge final : public STBase, public CountedObject<STXChainBridge>
{
    STAccount lockingChainDoor_{sfLockingChainDoor};
    STIssue lockingChainIssue_{sfLockingChainIssue};
    STAccount issuingChainDoor_{sfIssuingChainDoor};
    STIssue issuingChainIssue_{sfIssuingChainIssue};

public:
    /** Self-alias used by template code that calls `.value()` to strip the
     *  ST wrapper; for compound types the value type is the type itself. */
    using value_type = STXChainBridge;

    /** Identifies which of the two ledgers in a bridge a given door or asset
     *  belongs to. */
    enum class ChainType { Locking, Issuing };

    /** Returns the chain that is opposite to @p ct.
     *
     *  @param ct The chain whose counterpart is requested.
     *  @return `ChainType::Issuing` when @p ct is `Locking`; `Locking`
     *      otherwise.
     */
    static ChainType
    otherChain(ChainType ct);

    /** Maps the witness `wasLockingChainSend` flag to the originating chain.
     *
     *  Normalizes a boolean attestation flag into a `ChainType`, removing
     *  scattered `if (wasLockingChainSend)` branches from callers.
     *
     *  @param wasLockingChainSend `true` when the send originated on the
     *      locking chain.
     *  @return `ChainType::Locking` when @p wasLockingChainSend is `true`;
     *      `ChainType::Issuing` otherwise.
     */
    static ChainType
    srcChain(bool wasLockingChainSend);

    /** Maps the witness `wasLockingChainSend` flag to the destination chain.
     *
     *  Complement of `srcChain()`: returns the chain that receives the assets.
     *
     *  @param wasLockingChainSend `true` when the send originated on the
     *      locking chain.
     *  @return `ChainType::Issuing` when @p wasLockingChainSend is `true`;
     *      `ChainType::Locking` otherwise.
     */
    static ChainType
    dstChain(bool wasLockingChainSend);

    /** Constructs an empty bridge bound to `sfXChainBridge`.
     *
     *  Used as a canonical reference instance (e.g., to obtain the known
     *  JSON key set for extra-field detection in the JSON constructor).
     */
    STXChainBridge();

    /** Constructs an empty bridge bound to the given field name.
     *
     *  @param name The `SField` tag to associate with this object inside an
     *      enclosing `STObject`.
     */
    explicit STXChainBridge(SField const& name);

    STXChainBridge(STXChainBridge const& rhs) = default;

    /** Extracts bridge sub-fields from an already-parsed generic `STObject`.
     *
     *  Used during ledger deserialization when the parent has been parsed as
     *  an `STObject` and the four bridge fields must be projected into the
     *  strongly-typed form.
     *
     *  @param o Source object; must contain `sfLockingChainDoor`,
     *      `sfLockingChainIssue`, `sfIssuingChainDoor`, and
     *      `sfIssuingChainIssue` — `STObject::operator[]` throws `FieldErr`
     *      if any field is absent.
     */
    STXChainBridge(STObject const& o);

    /** Constructs a bridge from its four constituent values.
     *
     *  @param srcChainDoor  Door account on the locking chain.
     *  @param srcChainIssue Asset locked or released on the locking chain.
     *  @param dstChainDoor  Door account on the issuing chain.
     *  @param dstChainIssue Wrapped asset minted or burned on the issuing chain.
     */
    STXChainBridge(
        AccountID const& srcChainDoor,
        Issue const& srcChainIssue,
        AccountID const& dstChainDoor,
        Issue const& dstChainIssue);

    /** Deserializes a bridge from a JSON object, binding to `sfXChainBridge`.
     *
     *  Delegates to the two-argument form with `sfXChainBridge`.
     *
     *  @param v JSON object with keys `LockingChainDoor`, `LockingChainIssue`,
     *      `IssuingChainDoor`, `IssuingChainIssue`.
     *  @throws std::runtime_error if @p v is not an object, contains
     *      unrecognized keys, or either door field is not a valid Base58-encoded
     *      account.
     */
    explicit STXChainBridge(json::Value const& v);

    /** Deserializes a bridge from a JSON object, binding to @p name.
     *
     *  Performs a strict whitelist check against the canonical key set from a
     *  default-constructed bridge before parsing any values, rejecting typos
     *  and unknown fields at parse time rather than silently ignoring them.
     *
     *  @param name The `SField` to associate with this object.
     *  @param v    JSON object with the four bridge fields.
     *  @throws std::runtime_error if @p v is not an object, contains any key
     *      absent from the canonical set, or either door is not a valid
     *      Base58-encoded account.
     */
    explicit STXChainBridge(SField const& name, json::Value const& v);

    /** Deserializes a bridge from a binary stream.
     *
     *  Hot path for on-disk and network deserialization. Reads the four
     *  sub-fields in canonical order: locking door, locking issue, issuing
     *  door, issuing issue. Each sub-field consumes its own field-ID header
     *  and payload bytes from @p sit.
     *
     *  @param sit  Forward-only cursor positioned at the first byte of the
     *      bridge payload; advanced past all four fields on return.
     *  @param name The `SField` to associate with this object.
     */
    explicit STXChainBridge(SerialIter& sit, SField const& name);

    STXChainBridge&
    operator=(STXChainBridge const& rhs) = default;

    /** Returns a human-readable representation of the bridge for diagnostics.
     *
     *  Format: `{ LockingChainDoor = <addr>, LockingChainIssue = <issue>,
     *  IssuingChainDoor = <addr>, IssuingChainIssue = <issue> }`.
     *
     *  @return Formatted string; intended for logging and debug output only.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Converts this bridge into a generic `STObject` with the same four fields.
     *
     *  Needed when the bridge must participate in code paths that operate on
     *  `STObject` graphs, such as transaction metadata construction.
     *
     *  @return A new `STObject` bound to `sfXChainBridge` containing copies of
     *      all four bridge sub-fields.
     */
    [[nodiscard]] STObject
    toSTObject() const;

    /** Returns the door account of the locking chain. */
    [[nodiscard]] AccountID const&
    lockingChainDoor() const;

    /** Returns the asset locked or released on the locking chain. */
    [[nodiscard]] Issue const&
    lockingChainIssue() const;

    /** Returns the door account of the issuing chain. */
    [[nodiscard]] AccountID const&
    issuingChainDoor() const;

    /** Returns the wrapped asset minted or burned on the issuing chain. */
    [[nodiscard]] Issue const&
    issuingChainIssue() const;

    /** Returns the door account for the specified chain.
     *
     *  Allows generic code (e.g., attestation handlers) to query either side
     *  of a bridge without hard-coding which chain is locking vs. issuing.
     *  Pair with `srcChain()`/`dstChain()` to map a `wasLockingChainSend`
     *  boolean to the correct `ChainType`.
     *
     *  @param ct Which side of the bridge to query.
     *  @return The locking-chain door when @p ct is `Locking`; the
     *      issuing-chain door otherwise.
     */
    [[nodiscard]] AccountID const&
    door(ChainType ct) const;

    /** Returns the asset for the specified chain.
     *
     *  @param ct Which side of the bridge to query.
     *  @return The locking-chain issue when @p ct is `Locking`; the
     *      issuing-chain issue otherwise.
     */
    [[nodiscard]] Issue const&
    issue(ChainType ct) const;

    /** Returns `STI_XCHAIN_BRIDGE`, the type discriminator for this ST class. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Serializes the bridge to JSON.
     *
     *  Produces an object with keys `LockingChainDoor`, `LockingChainIssue`,
     *  `IssuingChainDoor`, `IssuingChainIssue`. The canonical key set from
     *  this output is also used by the JSON constructor to detect extra fields.
     *
     *  @return JSON object representation of all four bridge fields.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    /** Appends the binary encoding of all four sub-fields to @p s.
     *
     *  Each sub-field is written in canonical declaration order (locking door,
     *  locking issue, issuing door, issuing issue) and includes its own
     *  field-ID header, mirroring the `SerialIter` constructor's read order.
     *
     *  @param s Serializer accumulator to append to.
     */
    void
    add(Serializer& s) const override;

    /** Polymorphic equality check used by `STBase` container comparisons.
     *
     *  Performs a `dynamic_cast` to `STXChainBridge` and delegates to
     *  `operator==`. Returns `false` if @p t is not an `STXChainBridge`.
     *
     *  @param t The object to compare against.
     *  @return `true` iff @p t is an `STXChainBridge` with identical fields.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Returns `true` when all four sub-fields are in their default state. */
    [[nodiscard]] bool
    isDefault() const override;

    /** Returns a reference to this object itself.
     *
     *  Satisfies the convention that template code calling `.value()` on an
     *  ST type receives the unwrapped value. For compound types like
     *  `STXChainBridge`, `value_type` equals the type itself.
     *
     *  @return `*this`.
     */
    [[nodiscard]] value_type const&
    value() const noexcept;

private:
    static std::unique_ptr<STXChainBridge>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend bool
    operator==(STXChainBridge const& lhs, STXChainBridge const& rhs);

    friend bool
    operator<(STXChainBridge const& lhs, STXChainBridge const& rhs);
};

/** Returns `true` iff the two bridges have identical door accounts and assets.
 *
 *  Comparison is performed via `std::tie` across all four fields in
 *  declaration order: locking door, locking issue, issuing door, issuing issue.
 */
inline bool
operator==(STXChainBridge const& lhs, STXChainBridge const& rhs)
{
    return std::tie(
               lhs.lockingChainDoor_,
               lhs.lockingChainIssue_,
               lhs.issuingChainDoor_,
               lhs.issuingChainIssue_) ==
        std::tie(
               rhs.lockingChainDoor_,
               rhs.lockingChainIssue_,
               rhs.issuingChainDoor_,
               rhs.issuingChainIssue_);
}

/** Strict weak ordering over bridges; enables use as a `std::map`/`std::set` key.
 *
 *  Comparison is performed via `std::tie` across all four fields in
 *  declaration order: locking door, locking issue, issuing door, issuing issue.
 */
inline bool
operator<(STXChainBridge const& lhs, STXChainBridge const& rhs)
{
    return std::tie(
               lhs.lockingChainDoor_,
               lhs.lockingChainIssue_,
               lhs.issuingChainDoor_,
               lhs.issuingChainIssue_) <
        std::tie(
               rhs.lockingChainDoor_,
               rhs.lockingChainIssue_,
               rhs.issuingChainDoor_,
               rhs.issuingChainIssue_);
}

inline AccountID const&
STXChainBridge::lockingChainDoor() const
{
    return lockingChainDoor_.value();
};

inline Issue const&
STXChainBridge::lockingChainIssue() const
{
    return lockingChainIssue_.value().get<Issue>();
};

inline AccountID const&
STXChainBridge::issuingChainDoor() const
{
    return issuingChainDoor_.value();
};

inline Issue const&
STXChainBridge::issuingChainIssue() const
{
    return issuingChainIssue_.value().get<Issue>();
};

inline STXChainBridge::value_type const&
STXChainBridge::value() const noexcept
{
    return *this;
}

inline AccountID const&
STXChainBridge::door(ChainType ct) const
{
    if (ct == ChainType::Locking)
        return lockingChainDoor();
    return issuingChainDoor();
}

inline Issue const&
STXChainBridge::issue(ChainType ct) const
{
    if (ct == ChainType::Locking)
        return lockingChainIssue();
    return issuingChainIssue();
}

inline STXChainBridge::ChainType
STXChainBridge::otherChain(ChainType ct)
{
    if (ct == ChainType::Locking)
        return ChainType::Issuing;
    return ChainType::Locking;
}

inline STXChainBridge::ChainType
STXChainBridge::srcChain(bool wasLockingChainSend)
{
    if (wasLockingChainSend)
        return ChainType::Locking;
    return ChainType::Issuing;
}

inline STXChainBridge::ChainType
STXChainBridge::dstChain(bool wasLockingChainSend)
{
    if (wasLockingChainSend)
        return ChainType::Issuing;
    return ChainType::Locking;
}

}  // namespace xrpl
