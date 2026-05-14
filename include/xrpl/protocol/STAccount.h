#pragma once

/** @file
 *  Defines `STAccount`, the serialized-type wrapper for 160-bit XRPL account
 *  identifiers used inside transactions and ledger objects.
 *
 *  The internal storage is a plain `AccountID` (`base_uint<160>`) — no heap
 *  allocation — while the wire format deliberately preserves the
 *  variable-length (VL) blob encoding of the original `STBlob`-based
 *  implementation for byte-for-byte ledger compatibility.
 */

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STBase.h>

#include <string>

namespace xrpl {

/** Serialized-type wrapper for a 160-bit XRPL account identifier.
 *
 *  `STAccount` stores an `AccountID` value in a fixed-size `uint160` (no
 *  heap allocation) while serializing and deserializing using the
 *  VL-prefixed blob encoding of the legacy `STBlob` implementation, keeping
 *  the wire format byte-for-byte compatible with all existing ledger data.
 *
 *  A `bool default_` flag tracks whether the field has ever been explicitly
 *  assigned. A default field serializes as a zero-length VL blob, which is
 *  distinct from a field explicitly set to the all-zeros pseudo-account
 *  (`noAccount()`). Any call to `setValue()` or `operator=` clears the flag,
 *  even when the assigned value is zero.
 *
 *  Inherits `CountedObject<STAccount>` for lock-free diagnostic instance
 *  counting, and is `final` — no further derivation is expected.
 *
 *  @see STBase, CountedObject
 */
class STAccount final : public STBase, public CountedObject<STAccount>
{
private:
    // The original implementation kept the value in an STBlob.
    // But an STAccount is always 160 bits, so we can store it with less
    // overhead in an xrpl::uint160.  However, so the serialized format of the
    // STAccount stays unchanged, we serialize and deserialize like an STBlob.
    AccountID value_;
    bool default_;

public:
    using value_type = AccountID;

    /** Construct an anonymous, unset account field.
     *
     *  Sets the stored value to zero and marks the field as default (unset).
     *  A default field serializes as a zero-length VL blob and returns an
     *  empty string from `getText()`.
     */
    STAccount();

    /** Construct a named but unset account field.
     *
     *  Binds the field to `n` but leaves it in the default (unset) state.
     *  Typical use: pre-populating an `STObject` slot before the account
     *  address is known.
     *
     *  @param n The `SField` descriptor identifying this field (e.g. `sfAccount`).
     */
    STAccount(SField const& n);

    /** Construct from a raw VL-blob byte buffer.
     *
     *  An empty buffer is the canonical round-trip encoding of a default
     *  (unset) field and leaves the object in the default state. A non-empty
     *  buffer must be exactly 20 bytes; any other size throws.
     *
     *  @param n The `SField` descriptor for this field.
     *  @param v Raw bytes from a VL-blob read. Must be empty or exactly 20 bytes.
     *  @throws std::runtime_error if `v` is non-empty and not exactly 20 bytes.
     */
    STAccount(SField const& n, Buffer const& v);

    /** Deserialize an account field from a wire-format byte stream.
     *
     *  Extracts the next VL-prefixed blob from `sit` and delegates to the
     *  `Buffer` constructor for size validation and value assignment.
     *
     *  @param sit  Forward cursor over the serialized byte stream; advanced
     *      past the VL blob on return.
     *  @param name The `SField` descriptor for this field.
     *  @throws std::runtime_error if the extracted blob is not empty or 20 bytes.
     */
    STAccount(SerialIter& sit, SField const& name);

    /** Construct from a known `AccountID` value.
     *
     *  Marks the field as non-default regardless of whether `v` is the
     *  zero account. This is the standard path when the account address
     *  is already available at construction time.
     *
     *  @param n The `SField` descriptor for this field.
     *  @param v The 160-bit account identifier to store.
     */
    STAccount(SField const& n, AccountID const& v);

    /** Return the `SerializedTypeID` constant for this type (`STI_ACCOUNT`). */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return the account address as a Base58Check string, or empty if unset.
     *
     *  A default (unset) field returns `""` rather than the Base58 encoding
     *  of the all-zeros pseudo-account, preserving the distinction between an
     *  unset field and one explicitly set to `noAccount()`.
     *
     *  @return Base58Check-encoded address, or `""` when `isDefault()` is true.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Append this field to `s` using VL-blob wire encoding.
     *
     *  A default (unset) field serializes as a zero-length VL blob (one
     *  `0x00` byte on the wire). A non-default field serializes as a 20-byte
     *  VL blob. This preserves byte-for-byte compatibility with the legacy
     *  `STBlob`-based encoding and distinguishes "unset" from "explicitly set
     *  to the zero account."
     *
     *  @param s The `Serializer` to append to.
     *  @note Asserts (debug builds only) that the associated `SField` is a
     *      binary field of type `STI_ACCOUNT`.
     */
    void
    add(Serializer& s) const override;

    /** Check semantic equivalence with another serialized field.
     *
     *  Two `STAccount` objects are equivalent only when both their `default_`
     *  flags and their 160-bit values agree. The `SField` name is ignored —
     *  equivalence is purely about stored account state, not which field slot
     *  the object occupies.
     *
     *  @param t The field to compare against.
     *  @return `true` if `t` is an `STAccount` with the same default flag and
     *      value; `false` if `t` is a different type or either attribute differs.
     *  @note Callers that need to compare only the address (ignoring default
     *      state) should use `operator==` on the `value()` accessors directly.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` if this field has never been explicitly assigned.
     *
     *  A default field serializes as a zero-length VL blob. Assigning any
     *  `AccountID` — including the zero account — clears the default flag.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Assign an `AccountID` value, clearing the default flag.
     *
     *  @param value The account identifier to store.
     *  @return `*this`, to support chained assignments.
     */
    STAccount&
    operator=(AccountID const& value);

    /** Return the stored 160-bit account identifier.
     *
     *  Returns the underlying `AccountID` regardless of whether the field is
     *  in the default state. Callers that need to distinguish "unset" from
     *  a real zero account should check `isDefault()` first.
     */
    [[nodiscard]] AccountID const&
    value() const noexcept;

    /** Store `v` and mark this field as explicitly set.
     *
     *  Unconditionally clears the default flag, even when `v` is the zero
     *  account, so that `isDefault()` returns `false` after any call.
     *
     *  @param v The 160-bit account identifier to store.
     */
    void
    setValue(AccountID const& v);

private:
    /** Place a copy of this object into `buf` (if it fits within `n` bytes)
     *  or heap-allocate a copy via `STBase::emplace()`.
     *
     *  Used by `detail::STVar` for the small-object optimization.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Place a moved instance into `buf` (if it fits within `n` bytes)
     *  or heap-allocate via `STBase::emplace()`.
     *
     *  Used by `detail::STVar` for the small-object optimization.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STAccount&
STAccount::operator=(AccountID const& value)
{
    setValue(value);
    return *this;
}

inline AccountID const&
STAccount::value() const noexcept
{
    return value_;
}

inline void
STAccount::setValue(AccountID const& v)
{
    value_ = v;
    default_ = false;
}

/** Return `true` if both `STAccount` objects hold the same 160-bit value.
 *
 *  @note The default flag is not considered; use `isEquivalent()` when
 *      "set-ness" must also match.
 */
inline bool
operator==(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() == rhs.value();
}

/** Three-way-comparable less-than for two `STAccount` values. */
inline auto
operator<(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() < rhs.value();
}

/** Return `true` if the `STAccount` holds the same 160-bit value as `rhs`. */
inline bool
operator==(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() == rhs;
}

/** Less-than comparison between an `STAccount` and a raw `AccountID`. */
inline auto
operator<(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() < rhs;
}

/** Less-than comparison between a raw `AccountID` and an `STAccount`. */
inline auto
operator<(AccountID const& lhs, STAccount const& rhs)
{
    return lhs < rhs.value();
}

}  // namespace xrpl
