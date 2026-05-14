/** @file
 *  Defines `STCurrency`, the serialized-type wrapper for 160-bit XRPL
 *  currency identifiers used inside transactions and ledger objects.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Serialized-type wrapper for a 160-bit XRPL currency identifier.
 *
 *  `STCurrency` carries a `Currency` value (`base_uint<160,
 *  detail::CurrencyTag>`) inside the XRPL serialized-type field framework.
 *  Every field in a serialized transaction or ledger object must be an
 *  `STBase` subclass; `STCurrency` is the required adaptor for raw
 *  `Currency` values.
 *
 *  The default (zero) value represents native XRP: `isDefault()` returns
 *  `true` whenever `isXRP(currency_)` is true, which causes the field to
 *  be omitted from canonical serialization when it carries no information
 *  beyond "this is XRP."
 *
 *  Unlike `STAccount`, this class does not mix in `CountedObject`, so
 *  instance counts are not tracked for diagnostic purposes.
 *
 *  @see STAccount, STIssue, Currency
 */
class STCurrency final : public STBase
{
private:
    Currency currency_;

public:
    using value_type = Currency;

    /** Construct an anonymous default (XRP) currency field. */
    STCurrency() = default;

    /** Deserialize a currency field from a binary wire stream.
     *
     *  Reads exactly 160 bits from `sit` via `SerialIter::get160()`. No
     *  semantic validation is performed — binary data arriving from a
     *  consensus-validated ledger stream is assumed well-formed.
     *
     *  @param sit  Forward-only cursor over the serialized byte buffer;
     *      advanced by 20 bytes on return.
     *  @param name The `SField` descriptor for this field.
     */
    explicit STCurrency(SerialIter& sit, SField const& name);

    /** Construct a currency field with a known value.
     *
     *  The standard programmatic constructor used when the `Currency` is
     *  already available (e.g., when building a transaction in memory).
     *
     *  @param name     The `SField` descriptor for this field.
     *  @param currency The 160-bit currency identifier to store.
     */
    explicit STCurrency(SField const& name, Currency const& currency);

    /** Construct a named but default (XRP) currency field.
     *
     *  Binds the field to `name` and leaves the stored currency as the
     *  all-zeroes XRP value. Used when an `STObject` allocates a slot
     *  before the currency is known.
     *
     *  @param name The `SField` descriptor for this field.
     */
    explicit STCurrency(SField const& name);

    /** Return the stored 160-bit currency identifier. */
    [[nodiscard]] Currency const&
    currency() const;

    /** Return the stored 160-bit currency identifier.
     *
     *  Alias for `currency()`, provided so generic code that expects a
     *  `value()` accessor on all ST wrapper types works uniformly.
     */
    [[nodiscard]] Currency const&
    value() const noexcept;

    /** Replace the stored currency with `currency`.
     *
     *  @param currency The new 160-bit currency identifier.
     */
    void
    setCurrency(Currency const& currency);

    /** Return the `STI_CURRENCY` type tag used for field-dispatch and wire
     *  encoding.
     */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return a human-readable currency string.
     *
     *  Delegates to `to_string(currency_)`: returns `""` for XRP (zero),
     *  a three-character ISO-4217-style ticker (e.g., `"USD"`) for
     *  well-known tokens, or a hex string for opaque 160-bit custom
     *  currencies.
     *
     *  @return String representation of the stored currency.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Return a JSON string representation of the currency.
     *
     *  The output is identical to `getText()`. The `JsonOptions` argument
     *  is ignored — a currency code is always a plain string with no
     *  optional decoration.
     *
     *  @return JSON string value of the currency.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    /** Append the currency to `s` as a raw 20-byte bit string.
     *
     *  Writes the 160-bit value verbatim via `Serializer::addBitString`,
     *  with no VL prefix or other framing — consistent with all
     *  fixed-width scalar ST types.
     *
     *  @param s The `Serializer` accumulator to append to.
     */
    void
    add(Serializer& s) const override;

    /** Check semantic equivalence with another serialized field.
     *
     *  Uses `dynamic_cast` to confirm `t` is also an `STCurrency`, then
     *  compares the stored 160-bit values. Returns `false` for any other
     *  `STBase` subtype.
     *
     *  @param t The field to compare against.
     *  @return `true` if `t` is an `STCurrency` holding the same currency.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` when the stored currency is XRP (all-zeroes).
     *
     *  In `STBase` semantics, fields at their default value are omitted
     *  from canonical serialization. An `STCurrency` whose value is XRP
     *  need not carry an explicit currency code on the wire.
     */
    [[nodiscard]] bool
    isDefault() const override;

private:
    /** Factory called by `detail::STVar` when the wire type tag resolves
     *  to `STI_CURRENCY`. Delegates to the `SerialIter` constructor.
     */
    static std::unique_ptr<STCurrency>
    construct(SerialIter&, SField const& name);

    /** Place a copy of this object into `buf` (if it fits within `n`
     *  bytes) or heap-allocate via `STBase::emplace()`.
     *  Used by `detail::STVar` for the small-object optimization.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Place a moved instance into `buf` (if it fits within `n` bytes)
     *  or heap-allocate via `STBase::emplace()`.
     *  Used by `detail::STVar` for the small-object optimization.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

/** Parse and validate a currency field from a JSON value.
 *
 *  Acts as the defensive input validation gateway for API-sourced
 *  currency strings. Unlike binary deserialization, which trusts
 *  consensus-validated data, this function validates strictly because
 *  JSON input arrives from untrusted API consumers.
 *
 *  Accepts only JSON string values. The string is converted via
 *  `toCurrency()` and then checked against two sentinel values that
 *  `toCurrency()` may silently return for invalid input:
 *  - `noCurrency()` — syntactically invalid string.
 *  - `badCurrency()` — the string `"XRP"` used as an IOU ticker, which
 *      is explicitly prohibited to prevent confusion with native XRP.
 *
 *  @param name The `SField` descriptor for the resulting field.
 *  @param v    The JSON value to parse; must be a string.
 *  @return An `STCurrency` holding the validated, non-reserved currency.
 *  @throws std::runtime_error if `v` is not a string, or if the string
 *      resolves to `badCurrency()` or `noCurrency()`.
 */
STCurrency
currencyFromJson(SField const& name, json::Value const& v);

inline Currency const&
STCurrency::currency() const
{
    return currency_;
}

inline Currency const&
STCurrency::value() const noexcept
{
    return currency_;
}

inline void
STCurrency::setCurrency(Currency const& currency)
{
    currency_ = currency;
}

/** Return `true` if both `STCurrency` objects hold the same 160-bit value. */
inline bool
operator==(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() == rhs.currency();
}

/** Return `true` if the two `STCurrency` objects hold different values. */
inline bool
operator!=(STCurrency const& lhs, STCurrency const& rhs)
{
    return !operator==(lhs, rhs);
}

/** Less-than comparison between two `STCurrency` values, enabling use in
 *  sorted containers.
 */
inline bool
operator<(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() < rhs.currency();
}

/** Return `true` if the `STCurrency` holds the same 160-bit value as `rhs`.
 *
 *  Avoids constructing a temporary `STCurrency` when comparing a wrapped
 *  field directly against an unwrapped `Currency` value.
 */
inline bool
operator==(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() == rhs;
}

/** Less-than comparison between an `STCurrency` and a raw `Currency` value. */
inline bool
operator<(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() < rhs;
}

}  // namespace xrpl
