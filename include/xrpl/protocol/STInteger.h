#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>

namespace xrpl {

/** Wraps a plain integer inside the XRPL Serialized Type (ST) framework.
 *
 *  Every integer-valued field in a ledger entry, transaction, or metadata
 *  object — sequence numbers, flags, fees, timestamps, transaction types —
 *  is represented at runtime as one of the five concrete aliases defined
 *  below (`STUInt8`, `STUInt16`, `STUInt32`, `STUInt64`, `STInt32`).
 *
 *  The generic template supplies all methods that behave identically across
 *  integer widths (`add`, `isDefault`, `isEquivalent`, `operator=`,
 *  `copy`/`move` plumbing). Per-type specializations in `STInteger.cpp`
 *  provide `getSType()`, `getText()`, `getJson()`, and the deserialization
 *  constructor; these carry semantic knowledge that varies per instantiation
 *  (e.g., mapping `sfTransactionResult` bytes to TER strings, or rendering
 *  `STUInt64` as a JSON string to avoid IEEE 754 precision loss).
 *
 *  `CountedObject<STInteger<Integer>>` adds a lock-free per-instantiation
 *  instance counter, so the diagnostic system can report live `STUInt32` and
 *  `STUInt64` counts separately.
 *
 *  @tparam Integer The underlying C++ integer type (e.g., `std::uint32_t`).
 */
template <typename Integer>
class STInteger : public STBase, public CountedObject<STInteger<Integer>>
{
public:
    /** The underlying integer type wrapped by this instantiation. */
    using value_type = Integer;

private:
    Integer value_;

public:
    /** Construct an anonymous field holding @p v.
     *
     *  The field has no associated `SField` name (uses the generic placeholder).
     *  Prefer the two-argument constructor when the field will be stored in an
     *  `STObject`, so the protocol field identity is preserved.
     *
     *  @param v Initial value.
     */
    explicit STInteger(Integer v);

    /** Construct a named field holding @p v.
     *
     *  @param n The `SField` descriptor that identifies this field on the wire.
     *  @param v Initial value; defaults to zero.
     */
    STInteger(SField const& n, Integer v = 0);

    /** Deserialize from a wire byte stream.
     *
     *  Reads exactly `sizeof(Integer)` bytes from @p sit in big-endian order.
     *  Full specializations in `STInteger.cpp` provide the correct `sitGet*()`
     *  call for each instantiation width.
     *
     *  @param sit  Forward byte-stream cursor; advanced by `sizeof(Integer)`.
     *  @param name The `SField` descriptor to bind to the new object.
     *  @throws ripple::STObject::InvalidField or similar if @p sit underruns.
     */
    STInteger(SerialIter& sit, SField const& name);

    /** Return the `SerializedTypeID` tag for this integer width.
     *
     *  Full specializations return `STI_UINT8`, `STI_UINT16`, `STI_UINT32`,
     *  `STI_UINT64`, or `STI_INT32` as appropriate for `Integer`.
     */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Render the value to JSON, with field-identity-aware formatting.
     *
     *  Most instantiations emit the raw integer. Notable exceptions:
     *  - `STUInt8` on `sfTransactionResult` → short TER token string (e.g.,
     *    `"tesSUCCESS"`); raw integer on unrecognized codes.
     *  - `STUInt16` on `sfLedgerEntryType`/`sfTransactionType` → registered
     *    format name string (e.g., `"AccountRoot"`, `"Payment"`).
     *  - `STUInt32` on `sfPermissionValue` → permission name string.
     *  - `STUInt64` → always a JSON *string* (never a number) to avoid IEEE 754
     *    precision loss; hex by default, decimal if `SField::sMD_BaseTen` is set.
     *
     *  @param options Rendering flags (e.g., `JsonOptions::Values::None`).
     *  @return A `json::Value` representation of this field.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    /** Return a human-readable string representation of the value.
     *
     *  Applies the same field-identity-aware logic as `getJson()`, but returns
     *  a `std::string` suitable for diagnostics and logs rather than a JSON
     *  value. `STUInt8` on `sfTransactionResult` yields the long-form human
     *  description; `STUInt16` on `sfLedgerEntryType`/`sfTransactionType` yields
     *  the registered name; all others yield a decimal string.
     *
     *  @return Human-readable string for the field's value.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize the integer value into @p s.
     *
     *  Calls `s.addInteger(value_)`, which writes `sizeof(Integer)` bytes in
     *  big-endian order. Two `XRPL_ASSERT` guards fire in debug builds: one
     *  confirms the field is marked binary (`isBinary()`), and one confirms
     *  the field's declared type tag matches `getSType()`. A failing assert
     *  indicates a mis-wired field definition.
     *
     *  @param s The `Serializer` accumulator to write into.
     */
    void
    add(Serializer& s) const override;

    /** Return `true` if the wrapped value equals zero.
     *
     *  The ST framework uses this to omit optional fields whose value is the
     *  type default, keeping wire representations canonical and compact.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Return `true` if @p t holds the same concrete type and the same value.
     *
     *  Uses `dynamic_cast` to guard against comparing, say, an `STUInt32`
     *  with an `STUInt64` that happen to share the same bit pattern. Field
     *  names are ignored; only values are compared.
     *
     *  @param t The object to compare against.
     *  @return `true` if @p t is the same `STInteger<Integer>` instantiation
     *      and holds the same wrapped value.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Assign a new raw value, preserving the field identity.
     *
     *  @param v The new value to store.
     *  @return `*this`.
     */
    STInteger&
    operator=(value_type const& v);

    /** Return the wrapped value without implicit conversion.
     *
     *  Prefer this over `operator Integer()` when the intent is an explicit
     *  read; it is clearer at the call site that a raw integer is being
     *  extracted.
     */
    [[nodiscard]] value_type
    value() const noexcept;

    /** Replace the wrapped value.
     *
     *  @param v The new value to store.
     */
    void
    setValue(Integer v);

    /** Implicit conversion to the underlying integer type.
     *
     *  Allows `STInteger<T>` to be passed to functions expecting `T` without
     *  an explicit `.value()` call. Use `.value()` when clarity at the call
     *  site matters more than brevity.
     */
    operator Integer() const;

private:
    /** Copy this object into @p buf (or heap) via `STBase::emplace()`.
     *
     *  Called exclusively by `detail::STVar` to implement copy construction
     *  with the small-object optimization.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Move this object into @p buf (or heap) via `STBase::emplace()`.
     *
     *  Called exclusively by `detail::STVar` to implement move construction
     *  with the small-object optimization.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class xrpl::detail::STVar;
};

/** 8-bit unsigned serialized integer; used for `sfTransactionResult`. */
using STUInt8 = STInteger<unsigned char>;

/** 16-bit unsigned serialized integer; used for `sfLedgerEntryType` and `sfTransactionType`. */
using STUInt16 = STInteger<std::uint16_t>;

/** 32-bit unsigned serialized integer; the most common integer field width. */
using STUInt32 = STInteger<std::uint32_t>;

/** 64-bit unsigned serialized integer.
 *
 *  Always rendered as a JSON string (never a JSON number) to avoid IEEE 754
 *  precision loss. Fields annotated with `SField::sMD_BaseTen` render as
 *  decimal; all others render as lowercase hexadecimal.
 */
using STUInt64 = STInteger<std::uint64_t>;

/** 32-bit signed serialized integer. */
using STInt32 = STInteger<std::int32_t>;

template <typename Integer>
inline STInteger<Integer>::STInteger(Integer v) : value_(v)
{
}

template <typename Integer>
inline STInteger<Integer>::STInteger(SField const& n, Integer v) : STBase(n), value_(v)
{
}

template <typename Integer>
inline STBase*
STInteger<Integer>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <typename Integer>
inline STBase*
STInteger<Integer>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <typename Integer>
inline void
STInteger<Integer>::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STInteger::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == getSType(), "xrpl::STInteger::add : field type match");
    s.addInteger(value_);
}

template <typename Integer>
inline bool
STInteger<Integer>::isDefault() const
{
    return value_ == 0;
}

template <typename Integer>
inline bool
STInteger<Integer>::isEquivalent(STBase const& t) const
{
    STInteger const* v = dynamic_cast<STInteger const*>(&t);
    return v && (value_ == v->value_);
}

template <typename Integer>
inline STInteger<Integer>&
STInteger<Integer>::operator=(value_type const& v)
{
    value_ = v;
    return *this;
}

template <typename Integer>
inline typename STInteger<Integer>::value_type
STInteger<Integer>::value() const noexcept
{
    return value_;
}

template <typename Integer>
inline void
STInteger<Integer>::setValue(Integer v)
{
    value_ = v;
}

template <typename Integer>
inline STInteger<Integer>::
operator Integer() const
{
    return value_;
}

}  // namespace xrpl
