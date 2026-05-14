#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STTakesAsset.h>

#include <ostream>

namespace xrpl {

/** Serializable asset-contextual numeric field for XRPL ledger objects.
 *
 *  `STNumber` is effectively an `STAmount` without embedded `Asset` metadata.
 *  It stores only a `Number` (signed 64-bit mantissa + 32-bit exponent, 12
 *  bytes on the wire) and defers asset identity to runtime via the
 *  `STTakesAsset` mixin. This eliminates the per-field storage cost of
 *  duplicating asset information that is already present in the containing
 *  ledger entry (Vault, LoanBroker, Loan). All `NUMBER`-type SFields carry
 *  the `sMD_NeedsAsset` metadata flag; the free function
 *  `associateAsset(STLedgerEntry&, Asset const&)` walks a ledger entry and
 *  calls `associateAsset` on each such field near the end of `doApply()`.
 *
 *  Because `STNumber` provides `operator Number() const`, it can be passed
 *  directly wherever a `Number` is expected.
 *
 *  @note After `associateAsset()` is called, the stored value is rounded to
 *      the asset's canonical precision. Calling `setValue()` afterward
 *      without re-associating violates the two-phase rounding contract and
 *      will trigger an assertion in `add()`.
 *  @see STTakesAsset, STAmount, associateAsset(STLedgerEntry&, Asset const&)
 */
class STNumber : public STTakesAsset, public CountedObject<STNumber>
{
private:
    Number value_;

public:
    using value_type = Number;

    STNumber() = default;

    /** Construct an STNumber bound to the given SField with an initial value.
     *
     *  @param field The SField that identifies this value in its containing
     *      object. Must have `fieldType == STI_NUMBER`.
     *  @param value Initial numeric value; defaults to `Number()` (zero with
     *      sentinel exponent `std::numeric_limits<int>::lowest()`).
     */
    explicit STNumber(SField const& field, Number const& value = Number());

    /** Deserialize an STNumber from a byte stream.
     *
     *  Reads a 64-bit signed mantissa and a 32-bit signed exponent (12 bytes
     *  total) from @p sit. The two reads are issued as separate statements to
     *  guarantee evaluation order — merging them into a single call expression
     *  would produce undefined behavior because C++ does not sequence function
     *  arguments.
     *
     *  @param sit   Forward cursor positioned at the first byte of the payload.
     *  @param field The SField that identifies this value in its containing
     *      object.
     */
    STNumber(SerialIter& sit, SField const& field);

    /** @return `STI_NUMBER`. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** @return Decimal string representation of the stored `Number`. */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize the stored value as 12 bytes (int64 mantissa, int32 exponent).
     *
     *  For `sMD_NeedsAsset` fields this is Phase 2 of the two-phase rounding
     *  contract. When an asset has been associated, the value is re-rounded and
     *  asserted equal to the stored value, confirming that `associateAsset()`
     *  was called after the last `setValue()`. When no asset is present, a
     *  debug-only assertion verifies that `MantissaRange::Large` is active,
     *  because serializing under the small mantissa scale would silently
     *  truncate XRP/MPT integer values larger than 15 digits.
     *
     *  @param s Serializer accumulator to append to.
     */
    void
    add(Serializer& s) const override;

    /** @return Read-only reference to the stored `Number`. */
    [[nodiscard]] Number const&
    value() const;

    /** Replace the stored value without re-associating an asset.
     *
     *  @param v New value.
     *  @note If `associateAsset()` has already been called on this field,
     *      calling `setValue()` afterward without re-associating violates the
     *      two-phase rounding contract and will trigger an assertion in `add()`.
     */
    void
    setValue(Number const& v);

    /** Assign a new value; delegates to `setValue()`.
     *
     *  @param rhs New value.
     *  @return `*this`.
     */
    STNumber&
    operator=(Number const& rhs)
    {
        setValue(rhs);
        return *this;
    }

    /** @return `true` if the other `STBase` is an `STNumber` holding the same
     *      `Number` value.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** @return `true` if the stored value equals the default-constructed
     *      `Number()` (zero with its sentinel exponent), ensuring zero-valued
     *      fields round-trip correctly without false positives.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Bind an asset and immediately round the stored value to its precision.
     *
     *  Phase 1 of the two-phase rounding contract. Stores @p a via
     *  `STTakesAsset::associateAsset` then calls `roundToAsset(a, value_)`.
     *  For XRP and MPT this truncates fractional drops; for IOU this normalises
     *  to 15 significant decimal digits. After this call, `add()` will assert
     *  idempotency on the rounded value.
     *
     *  @param a Asset whose precision governs rounding.
     *  @note The field must carry the `sMD_NeedsAsset` metadata flag; a debug
     *      assertion fires if it does not.
     */
    void
    associateAsset(Asset const& a) override;

    /** Implicit conversion to `Number`, enabling use in numeric expressions.
     *
     *  @return A copy of the stored `Number`.
     */
    operator Number() const
    {
        return value_;
    }

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;
};

/** Write the decimal string representation of @p rhs to @p out.
 *
 *  @param out Output stream to write to.
 *  @param rhs The value to render.
 *  @return @p out.
 */
std::ostream&
operator<<(std::ostream& out, STNumber const& rhs);

/** Raw parsed components of a decimal number string.
 *
 *  Produced by `partsFromString()` before normalization. The mantissa is
 *  always unsigned; sign is carried separately in `negative`.
 */
struct NumberParts
{
    /** Unsigned integer formed by concatenating integer and fractional digits. */
    std::uint64_t mantissa = 0;
    /** Exponent adjusted for fractional digit count and any explicit `e` suffix. */
    int exponent = 0;
    /** `true` if the original string had a leading `'-'`. */
    bool negative = false;
};

/** Parse a decimal string into its raw mantissa/exponent/sign components.
 *
 *  Accepts an optional leading sign, a non-empty integer part (no leading
 *  zeroes unless the value is exactly `"0"`), an optional fractional part,
 *  and an optional `e`/`E` exponent suffix. No normalization is applied —
 *  the caller receives the raw parsed representation.
 *
 *  @param number Decimal string to parse (e.g., `"3.14e2"`, `"-42"`, `"0"`).
 *  @return `NumberParts` with unsigned mantissa, adjusted exponent, and sign.
 *  @throws std::runtime_error if @p number does not match the expected format
 *      (e.g., empty string, leading zeroes, bare `"e"`, trailing decimal point).
 *  @throws std::bad_cast (via `boost::lexical_cast`) if the digit string
 *      overflows `uint64_t`.
 *  @note The backing regex is compiled once as a `static` local with the
 *      `optimize` flag to amortize construction cost across calls.
 */
NumberParts
partsFromString(std::string const& number);

/** Construct an STNumber from a JSON integer or decimal string.
 *
 *  Dispatches on the JSON value type:
 *  - **Integer** (`isInt`/`isUInt`): reads the native integer value directly.
 *  - **String**: delegates to `partsFromString()`; this path asserts that no
 *    active transaction rules are present, restricting use to pre-transactor
 *    JSON deserialization (e.g., `STParsedJSON`).
 *  - Anything else throws.
 *
 *  @param field The SField that identifies the resulting STNumber.
 *  @param value JSON node containing the numeric value.
 *  @return A new STNumber holding the parsed value, not yet asset-rounded.
 *  @throws std::runtime_error if @p value is not an integer or string, or if
 *      a string fails to parse as a valid decimal.
 *  @throws std::bad_cast (via `boost::lexical_cast`) if a string mantissa
 *      overflows `uint64_t`.
 *  @note String-format numbers are forbidden during active transaction
 *      processing; only numeric JSON types are accepted in that context.
 */
STNumber
numberFromJson(SField const& field, json::Value const& value);

}  // namespace xrpl
