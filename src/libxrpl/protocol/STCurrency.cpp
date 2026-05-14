/** @file
 *  Implements STCurrency, the serialized-type wrapper for 160-bit XRPL
 *  currency identifiers, and the JSON deserialization helper
 *  `currencyFromJson`.
 */

#include <xrpl/protocol/STCurrency.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Construct a default (XRP) currency field for the given SField slot.
 *
 *  Used when an STObject allocates a placeholder for a field that has not
 *  yet been populated. The stored currency is the all-zeroes value, which
 *  represents native XRP.
 *
 *  @param name The SField descriptor that identifies this field within its
 *      parent STObject.
 */
STCurrency::STCurrency(SField const& name) : STBase{name}
{
}

/** Deserialize a currency field from a binary stream.
 *
 *  Reads exactly 160 bits from `sit` via `SerialIter::get160()` and stores
 *  the result verbatim. No validation is performed: binary ledger data
 *  originates from a consensus-validated stream and re-checking every field
 *  on ingestion would be prohibitively expensive.
 *
 *  @param sit The forward-only cursor over the serialized byte buffer.
 *  @param name The SField descriptor for this field.
 */
STCurrency::STCurrency(SerialIter& sit, SField const& name) : STBase{name}
{
    currency_ = sit.get160();
}

/** Construct a currency field with a known Currency value.
 *
 *  Used programmatically when the Currency is already available (e.g., when
 *  building a transaction in memory).
 *
 *  @param name The SField descriptor for this field.
 *  @param currency The 160-bit currency identifier to store.
 */
STCurrency::STCurrency(SField const& name, Currency const& currency)
    : STBase{name}, currency_{currency}
{
}

/** @return The `STI_CURRENCY` type tag for field-dispatch and wire encoding. */
SerializedTypeID
STCurrency::getSType() const
{
    return STI_CURRENCY;
}

/** @return The human-readable currency string: empty for XRP (zero), the
 *      ISO-4217-style three-character ticker for well-known tokens, or a
 *      hex string for opaque custom currencies.
 */
std::string
STCurrency::getText() const
{
    return to_string(currency_);
}

/** @return A JSON string representation of the currency, identical to
 *      `getText()`.
 */
json::Value
STCurrency::getJson(JsonOptions) const
{
    return to_string(currency_);
}

/** Serialize the currency by appending its 160-bit value verbatim.
 *
 *  @param s The Serializer accumulator to write into.
 */
void
STCurrency::add(Serializer& s) const
{
    s.addBitString(currency_);
}

/** Compare this field to another STBase for value equality.
 *
 *  Uses `dynamic_cast` to confirm `t` is also an `STCurrency` before
 *  comparing the stored 160-bit values. Returns `false` for any other
 *  STBase subtype, consistent with the polymorphic comparison contract
 *  across the ST hierarchy.
 *
 *  @param t The other serialized field to compare against.
 *  @return `true` if `t` is an `STCurrency` holding the same currency.
 */
bool
STCurrency::isEquivalent(STBase const& t) const
{
    STCurrency const* v = dynamic_cast<STCurrency const*>(&t);
    return (v != nullptr) && (*v == *this);
}

/** @return `true` when the stored currency is XRP (all-zeroes).
 *
 *  In STBase semantics, "default" fields are omitted from canonical
 *  serialization, so a field whose currency is XRP need not carry an
 *  explicit currency code on the wire.
 */
bool
STCurrency::isDefault() const
{
    return isXRP(currency_);
}

/** Factory for the STVar deserialization registry.
 *
 *  Called by `detail::STVar` when the wire type tag resolves to
 *  `STI_CURRENCY`. Delegates to the `SerialIter` constructor.
 *
 *  @param sit The binary cursor to deserialize from.
 *  @param name The SField descriptor for this field.
 *  @return A heap-allocated STCurrency owning the deserialized value.
 */
std::unique_ptr<STCurrency>
STCurrency::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STCurrency>(sit, name);
}

/** Copy this field into a caller-supplied buffer using the STVar small-buffer
 *  optimization.
 *
 *  Delegates to `STBase::emplace`: if `sizeof(STCurrency)` fits within `n`
 *  bytes, the copy is placement-new'd into `buf`; otherwise a heap allocation
 *  is returned.
 *
 *  @param n Capacity of `buf` in bytes.
 *  @param buf Pointer to inline storage offered by the enclosing STVar.
 *  @return Pointer to the newly constructed copy (either `buf` or heap).
 */
STBase*
STCurrency::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Move this field into a caller-supplied buffer using the STVar small-buffer
 *  optimization.
 *
 *  Delegates to `STBase::emplace` with move semantics. See `copy()` for the
 *  buffer-selection logic.
 *
 *  @param n Capacity of `buf` in bytes.
 *  @param buf Pointer to inline storage offered by the enclosing STVar.
 *  @return Pointer to the newly constructed object (either `buf` or heap).
 */
STBase*
STCurrency::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Parse and validate a currency field from a JSON value.
 *
 *  Accepts only string JSON values. The string is converted via
 *  `toCurrency()` and then checked against two sentinel values:
 *
 *  - `noCurrency()` — returned by `toCurrency()` when the string is
 *    syntactically invalid.
 *  - `badCurrency()` — returned by `toCurrency()` for the three-letter
 *    string `"XRP"` used as a token identifier, which is explicitly
 *    prohibited to prevent confusion with native XRP.
 *
 *  Both sentinels are rejected so callers receive a clean guarantee: if
 *  this function returns, the `STCurrency` holds a well-formed,
 *  non-reserved currency code. Unlike the binary deserialization path,
 *  this function validates strictly because JSON input arrives from
 *  untrusted API consumers.
 *
 *  @param name The SField descriptor for the resulting field.
 *  @param v The JSON value to parse; must be a string.
 *  @return An STCurrency holding the validated currency.
 *  @throws std::runtime_error if `v` is not a string, or if the string
 *      resolves to `badCurrency()` or `noCurrency()`.
 */
STCurrency
currencyFromJson(SField const& name, json::Value const& v)
{
    if (!v.isString())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a string Json value");
    }

    auto const currency = toCurrency(v.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a valid currency");
    }

    return STCurrency{name, currency};
}

}  // namespace xrpl
