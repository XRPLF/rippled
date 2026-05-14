/** @file
 *  Implements STNumber, the serializable precision-number field type for
 *  XRPL ledger objects that store asset amounts without redundant asset identity.
 *
 *  STNumber stores only a mantissa+exponent pair (a `Number`) on the wire
 *  (12 bytes: int64 mantissa + int32 exponent).  Asset binding is deferred to
 *  runtime via the `STTakesAsset` mixin so that ledger objects such as Vault,
 *  LoanBroker, and Loan — where many numeric fields all refer to the same asset
 *  — do not pay the storage cost of duplicating asset identity in every field.
 */

#include <xrpl/protocol/STNumber.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/Serializer.h>

#include <boost/lexical_cast.hpp>
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

STNumber::STNumber(SField const& field, Number const& value) : STTakesAsset(field), value_(value)
{
}

/** Deserialize an STNumber from a byte stream.
 *
 *  Reads a 64-bit signed mantissa followed by a 32-bit exponent from @p sit.
 *  The two reads are in separate statements to guarantee sequencing — C++
 *  does not specify argument-evaluation order within a single call expression,
 *  so merging them into `Number{sit.geti64(), sit.geti32()}` would produce
 *  undefined behavior.
 *
 *  @param sit   Cursor positioned at the start of the 12-byte payload.
 *  @param field The SField that identifies this value in its containing object.
 */
STNumber::STNumber(SerialIter& sit, SField const& field) : STTakesAsset(field)
{
    // Separate statements guarantee evaluation order (geti64 before geti32).
    auto mantissa = sit.geti64();
    auto exponent = sit.geti32();
    value_ = Number{mantissa, exponent};
}

SerializedTypeID
STNumber::getSType() const
{
    return STI_NUMBER;
}

std::string
STNumber::getText() const
{
    return to_string(value_);
}

/** Bind an Asset to this field and round the stored value to its precision.
 *
 *  Phase 1 of the two-phase rounding contract: stores the asset via
 *  `STTakesAsset::associateAsset` and immediately rounds `value_` to the
 *  asset's canonical precision via `roundToAsset`.  For XRP and MPT this
 *  means truncating fractional drops; for IOU this means normalising to
 *  15 significant decimal digits.  After this call, `add()` will assert
 *  idempotency — calling `setValue()` without re-associating afterward is
 *  a programming error.
 *
 *  @param a The asset whose precision governs rounding.
 *  @note The field must carry the `sMD_NeedsAsset` metadata flag; the
 *      assertion will fire in debug builds if it does not.
 */
void
STNumber::associateAsset(Asset const& a)
{
    STTakesAsset::associateAsset(a);

    XRPL_ASSERT_PARTS(
        getFName().shouldMeta(SField::kSMD_NEEDS_ASSET),
        "STNumber::associateAsset",
        "field needs asset");

    roundToAsset(a, value_);
}

/** Serialize this value as 12 bytes: int64 mantissa followed by int32 exponent.
 *
 *  For `sMD_NeedsAsset` fields this is Phase 2 of the two-phase rounding
 *  contract.  When an asset is present the value is rounded again (via
 *  `roundToAsset`) and the result is asserted to equal `value_`, verifying
 *  idempotency — any mismatch means `setValue()` was called after
 *  `associateAsset()` without re-associating, which would produce incorrect
 *  ledger state.  When no asset is present (e.g., an already-rounded value
 *  being re-serialized without going through a transactor), a debug-only
 *  assertion confirms that `MantissaRange::Large` is active, because
 *  serializing under `Small` scale would silently truncate precision.
 *
 *  The mantissa range assertion guards the wire format: although
 *  `Number::mantissa()` returns `int64_t`, the internal representation uses an
 *  unsigned 64-bit value extended to 19 digits under `Large` scale, and the
 *  accessor divides by 10 when necessary; the assertion documents that the
 *  result must always fit in a signed 64-bit wire field.
 *
 *  @param s Serializer accumulator to append to.
 */
void
STNumber::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STNumber::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == getSType(), "xrpl::STNumber::add : field type match");

    auto value = value_;
    auto const mantissa = value.mantissa();
    auto const exponent = value.exponent();

    SField const& field = getFName();
    if (field.shouldMeta(SField::kSMD_NEEDS_ASSET))
    {
        if (asset_)
        {
            // Phase 2 idempotency check: re-round and assert the stored value
            // was already rounded by associateAsset().
            roundToAsset(*asset_, value);
            XRPL_ASSERT_PARTS(value_ == value, "xrpl::STNumber::add", "value is already rounded");
        }
        else
        {
#if !NDEBUG
            // Serializing without an asset (e.g., a pass-through re-serialization
            // that bypassed a transactor). We cannot verify rounding, but we can
            // assert the mantissa scale is Large — using Small scale here would
            // silently truncate XRP/MPT integer values that exceed 15 digits.
            XRPL_ASSERT_PARTS(
                Number::getMantissaScale() == MantissaRange::MantissaScale::Large,
                "xrpl::STNumber::add",
                "STNumber only used with large mantissa scale");
#endif
        }
    }

    XRPL_ASSERT_PARTS(
        mantissa <= std::numeric_limits<std::int64_t>::max() &&
            mantissa >= std::numeric_limits<std::int64_t>::min(),
        "xrpl::STNumber::add",
        "mantissa in valid range");
    s.add64(mantissa);
    s.add32(exponent);
}

Number const&
STNumber::value() const
{
    return value_;
}

void
STNumber::setValue(Number const& v)
{
    value_ = v;
}

STBase*
STNumber::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STNumber::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

bool
STNumber::isEquivalent(STBase const& t) const
{
    XRPL_ASSERT(
        t.getSType() == this->getSType(), "xrpl::STNumber::isEquivalent : field type match");
    STNumber const& v = dynamic_cast<STNumber const&>(t);
    return value_ == v;
}

bool
STNumber::isDefault() const
{
    return value_ == Number();
}

std::ostream&
operator<<(std::ostream& out, STNumber const& rhs)
{
    return out << rhs.getText();
}

/** Parse a decimal string into raw mantissa/exponent/sign parts.
 *
 *  Accepts optional sign, integer part (no leading zeroes unless the value is
 *  exactly `"0"`), optional fractional part, and optional `e`/`E` exponent.
 *  The fractional digits are concatenated with the integer digits to form the
 *  mantissa; the exponent is adjusted by the negative of the fractional digit
 *  count, then shifted by any explicit exponent.  No normalization is applied —
 *  the caller receives the raw parsed representation.
 *
 *  @param number Decimal string to parse (e.g., `"3.14e2"`, `"-42"`, `"0"`).
 *  @return Parsed `NumberParts` with unsigned mantissa, adjusted exponent, and
 *      sign flag.
 *  @throws std::runtime_error if the string does not match the expected decimal
 *      format (e.g., leading zeroes, dangling decimal point, bare `"e"`,
 *      empty string).
 *  @throws std::bad_cast (from `boost::lexical_cast`) if the digit string
 *      overflows `uint64_t` (e.g., a 200-digit integer literal).
 *  @note The regex is compiled once as a `static` local with the `optimize`
 *      flag to amortize construction cost across calls.
 */
NumberParts
partsFromString(std::string const& number)
{
    static boost::regex const kRE_NUMBER(
        "^"                       // the beginning of the string
        "([-+]?)"                 // (optional) + or - character
        "(0|[1-9][0-9]*)"         // a number (no leading zeroes, unless 0)
        "(\\.([0-9]+))?"          // (optional) period followed by any number
        "([eE]([+-]?)([0-9]+))?"  // (optional) E, optional + or -, any number
        "$",
        boost::regex_constants::optimize);

    boost::smatch match;

    if (!boost::regex_match(number, match, kRE_NUMBER))
        Throw<std::runtime_error>("'" + number + "' is not a number");

    bool const negative = (match[1].matched && (match[1] == "-"));

    std::uint64_t mantissa = 0;
    int exponent = 0;

    if (!match[4].matched)
    {
        mantissa = boost::lexical_cast<std::uint64_t>(std::string(match[2]));
        exponent = 0;
    }
    else
    {
        mantissa = boost::lexical_cast<std::uint64_t>(match[2] + match[4]);
        exponent = -(match[4].length());
    }

    if (match[5].matched)
    {
        if (match[6].matched && (match[6] == "-"))
        {
            exponent -= boost::lexical_cast<int>(std::string(match[7]));
        }
        else
        {
            exponent += boost::lexical_cast<int>(std::string(match[7]));
        }
    }

    return {.mantissa = mantissa, .exponent = exponent, .negative = negative};
}

/** Construct an STNumber from a JSON value.
 *
 *  Dispatches on the JSON value type:
 *  - **Integer** (`isInt`/`isUInt`): reads the native integer value directly,
 *    preserving sign for signed integers via `asAbsUInt()` + `negative` flag.
 *  - **String**: delegates to `partsFromString`; this path asserts that no
 *    active transaction rules are present (`getCurrentTransactionRules()` is
 *    null), because accepting user-supplied decimal strings inside a transactor
 *    would expose precision-sensitive parsing to untrusted input.
 *  - Anything else throws.
 *
 *  @param field The SField that identifies the resulting STNumber.
 *  @param value JSON node containing the numeric value.
 *  @return A new STNumber holding the parsed value (not yet asset-rounded).
 *  @throws std::runtime_error if @p value is not an integer or string, or if
 *      the string fails to parse as a valid decimal.
 *  @throws std::bad_cast (via `boost::lexical_cast`) if a string mantissa
 *      overflows `uint64_t`.
 *  @note String input is forbidden during transaction processing; only numeric
 *      JSON types are accepted in that context.
 */
STNumber
numberFromJson(SField const& field, json::Value const& value)
{
    NumberParts parts;

    if (value.isInt())
    {
        if (value.asInt() >= 0)
        {
            parts.mantissa = value.asInt();
        }
        else
        {
            parts.mantissa = value.asAbsUInt();
            parts.negative = true;
        }
    }
    else if (value.isUInt())
    {
        parts.mantissa = value.asUInt();
    }
    else if (value.isString())
    {
        parts = partsFromString(value.asString());

        XRPL_ASSERT_PARTS(
            !getCurrentTransactionRules(), "xrpld::numberFromJson", "Not in a Transactor context");

        // Number mantissas are much bigger than the allowable parsed values, so
        // it can't be out of range.
        static_assert(
            // NOLINTNEXTLINE(misc-redundant-expression)
            std::numeric_limits<std::uint64_t>::max() >=
            std::numeric_limits<decltype(parts.mantissa)>::max());
    }
    else
    {
        Throw<std::runtime_error>("not a number");
    }

    return STNumber{
        field, Number{parts.negative, parts.mantissa, parts.exponent, Number::Normalized{}}};
}

}  // namespace xrpl
