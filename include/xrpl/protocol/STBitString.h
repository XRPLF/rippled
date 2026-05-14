/** @file
 *  Defines `STBitString<Bits>`, the serialization-layer representation of
 *  fixed-width opaque bit arrays, and the four concrete aliases used
 *  throughout the protocol: `STUInt128`, `STUInt160`, `STUInt192`, and
 *  `STUInt256`.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STBase.h>

namespace xrpl {

/** Serialized fixed-width bit string field in the XRPL protocol type system.
 *
 *  Bridges `BaseUInt<Bits>` — used for transaction hashes, account IDs,
 *  ledger indices, and similar opaque identifiers — and the `STBase`
 *  serialization framework.  Despite the underlying type supporting
 *  arithmetic, this class treats its value as an opaque sequence of bits:
 *  only identity comparison, serialization, and value access are exposed.
 *
 *  Each concrete instantiation (128, 160, 192, 256 bits) returns a
 *  distinct wire-type code from `getSType()` (`STI_UINT128` through
 *  `STI_UINT256`), so field metadata and codec behavior are type-correct at
 *  the protocol level.
 *
 *  `CountedObject<STBitString<Bits>>` maintains a per-width live instance
 *  counter for diagnostic reporting; it carries no functional overhead.
 *
 *  @tparam Bits Number of bits in the value.  Declared `int` rather than
 *      `unsigned int` to work around a GDB 12.1 bug that prevents locating
 *      RTTI for templates instantiated over unsigned types; a `static_assert`
 *      enforces that the value is positive.
 */
template <int Bits>
class STBitString final : public STBase, public CountedObject<STBitString<Bits>>
{
    static_assert(Bits > 0, "Number of bits must be positive");

public:
    /** The underlying tag-free bit-string type (`BaseUInt<Bits>`). */
    using value_type = BaseUInt<Bits>;

private:
    value_type value_{};

public:
    STBitString() = default;

    /** Construct a named field with a zero-initialized value.
     *
     *  Used when building objects programmatically before the value is known.
     *
     *  @param n The `SField` that identifies this field on the wire.
     */
    STBitString(SField const& n);

    /** Construct an anonymous value, discarding field identity.
     *
     *  Intended for temporary computations where only the raw value matters.
     *
     *  @param v The initial bit-string value.
     */
    STBitString(value_type const& v);

    /** Construct a fully specified named field with a given value.
     *
     *  @param n The `SField` that identifies this field on the wire.
     *  @param v The initial bit-string value.
     */
    STBitString(SField const& n, value_type const& v);

    /** Deserialize a named field from a byte stream.
     *
     *  Reads exactly `Bits/8` bytes from `sit` at the current cursor
     *  position via `SerialIter::getBitString<Bits>()`, centralizing
     *  deserialization logic in `SerialIter`.
     *
     *  @param sit   The input cursor; advanced by `Bits/8` bytes on success.
     *  @param name  The `SField` that identifies this field on the wire.
     *  @throws std::runtime_error if the stream has fewer than `Bits/8`
     *      bytes remaining.
     */
    STBitString(SerialIter& sit, SField const& name);

    /** Return the wire-type identifier for this bit width.
     *
     *  Specialized for each concrete alias: `STI_UINT128`, `STI_UINT160`,
     *  `STI_UINT192`, and `STI_UINT256`.
     *
     *  @return The `SerializedTypeID` matching this instantiation's bit width.
     */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return the hex-encoded string representation of the stored value.
     *
     *  @return A lowercase hex string with no prefix.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Test whether this field holds the same value as another `STBitString`.
     *
     *  @param t  The object to compare against.
     *  @return `true` if `t` is the same concrete bit width and both values
     *      are equal; `false` otherwise (including when `t` has a different
     *      bit width).
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Serialize the value into a `Serializer` byte buffer.
     *
     *  Writes exactly `Bits/8` bytes to `s` via `Serializer::addBitString`.
     *
     *  @param s The accumulator to write into.
     *  @pre `getFName().isBinary()` must be `true`.
     *  @pre `getFName().fieldType` must equal `getSType()`.
     *  @note Both preconditions are checked with `XRPL_ASSERT`; violations
     *      indicate a field/type metadata mismatch that would cause silent
     *      protocol corruption.
     */
    void
    add(Serializer& s) const override;

    /** Return `true` when the stored value is the all-zeros bit string.
     *
     *  The serialization layer uses this to decide whether a field may be
     *  omitted from canonical binary encoding.
     *
     *  @return `true` if the value equals `beast::zero`.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Assign a new value, accepting any tag variant of `BaseUInt<Bits>`.
     *
     *  The free `Tag` template parameter allows cross-tag assignment (e.g.,
     *  assigning a raw `uint256` to an `sfTransactionID` field) when the
     *  caller explicitly intends it, while making accidental mixing visible
     *  at the call site.  Tag information is erased on storage.
     *
     *  @tparam Tag  The source tag type; any `BaseUInt<Bits, Tag>` is accepted.
     *  @param v     The new value.
     */
    template <typename Tag>
    void
    setValue(BaseUInt<Bits, Tag> const& v);

    /** Return a const reference to the stored tag-free value.
     *
     *  @return Reference to the internal `value_type`; valid for the lifetime
     *      of this object.
     */
    [[nodiscard]] value_type const&
    value() const;

    /** Implicit conversion to the tag-free `value_type`.
     *
     *  Erases any tag information from the underlying `BaseUInt` on the way
     *  out.  Prefer `value()` in generic code to make the conversion explicit.
     */
    operator value_type() const;

private:
    /** Place-construct a copy into `buf` if it fits within `n` bytes;
     *  otherwise heap-allocate.  Called by `detail::STVar` for the
     *  small-object optimisation inside `STObject` containers.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Place-construct a moved instance into `buf` if it fits within `n`
     *  bytes; otherwise heap-allocate.  Called by `detail::STVar`.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

/** Serialized 128-bit opaque bit string (wire type `STI_UINT128`). */
using STUInt128 = STBitString<128>;

/** Serialized 160-bit opaque bit string (wire type `STI_UINT160`).
 *
 *  Used for `AccountID` fields and similar 20-byte identifiers.
 */
using STUInt160 = STBitString<160>;

/** Serialized 192-bit opaque bit string (wire type `STI_UINT192`).
 *
 *  Used for `MPTID` fields (32-bit sequence number ‖ 160-bit issuer).
 */
using STUInt192 = STBitString<192>;

/** Serialized 256-bit opaque bit string (wire type `STI_UINT256`).
 *
 *  The most commonly used alias; carries transaction hashes, ledger
 *  indices, node IDs, and other 32-byte protocol identifiers.
 */
using STUInt256 = STBitString<256>;

template <int Bits>
inline STBitString<Bits>::STBitString(SField const& n) : STBase(n)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(value_type const& v) : value_(v)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(SField const& n, value_type const& v) : STBase(n), value_(v)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(SerialIter& sit, SField const& name)
    : STBitString(name, sit.getBitString<Bits>())
{
}

template <int Bits>
STBase*
STBitString<Bits>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <int Bits>
STBase*
STBitString<Bits>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <>
inline SerializedTypeID
STUInt128::getSType() const
{
    return STI_UINT128;
}

template <>
inline SerializedTypeID
STUInt160::getSType() const
{
    return STI_UINT160;
}

template <>
inline SerializedTypeID
STUInt192::getSType() const
{
    return STI_UINT192;
}

template <>
inline SerializedTypeID
STUInt256::getSType() const
{
    return STI_UINT256;
}

template <int Bits>
std::string
STBitString<Bits>::getText() const
{
    return to_string(value_);
}

template <int Bits>
bool
STBitString<Bits>::isEquivalent(STBase const& t) const
{
    STBitString const* v = dynamic_cast<STBitString const*>(&t);
    return v && (value_ == v->value_);
}

template <int Bits>
void
STBitString<Bits>::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STBitString::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == getSType(), "xrpl::STBitString::add : field type match");
    s.addBitString<Bits>(value_);
}

template <int Bits>
template <typename Tag>
void
STBitString<Bits>::setValue(BaseUInt<Bits, Tag> const& v)
{
    value_ = v;
}

template <int Bits>
typename STBitString<Bits>::value_type const&
STBitString<Bits>::value() const
{
    return value_;
}

template <int Bits>
STBitString<Bits>::
operator value_type() const
{
    return value_;
}

template <int Bits>
bool
STBitString<Bits>::isDefault() const
{
    return value_ == beast::kZERO;
}

}  // namespace xrpl
