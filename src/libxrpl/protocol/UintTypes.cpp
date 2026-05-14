/** @file
 *  Currency code serialization for the XRPL protocol.
 *
 *  Implements the canonical mapping between 160-bit `Currency` values and
 *  human-readable strings, and defines the three protocol sentinel currencies
 *  (`xrpCurrency`, `noCurrency`, `badCurrency`).
 *
 *  @see https://xrpl.org/serialization.html#currency-codes
 */

#include <xrpl/protocol/UintTypes.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/SystemParameters.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace xrpl {

namespace detail {

/** Characters accepted in a three-character ISO-style currency code.
 *
 *  This set is intentionally broader than strict ISO 4217 (which allows only
 *  `[A-Z]`), accommodating XRPL's extended custom-currency ecosystem.
 *  Any 3-byte sequence composed entirely of these characters is treated as an
 *  ISO-style code during both parsing and serialization.
 */
constexpr std::string_view kISO_CHAR_SET =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "<>(){}[]|?!@#$%^&*";

/** Byte offset of the 3-character currency code within a 160-bit Currency value.
 *
 *  Per the XRPL serialization spec, bytes 0–11 and 15–19 of an ISO-style
 *  currency must be zero; the ASCII code occupies bytes 12–14.
 */
constexpr std::size_t kISO_CODE_OFFSET = 12;

/** Length in bytes of an ISO-style currency code. */
constexpr std::size_t kISO_CODE_LENGTH = 3;

}  // namespace detail

/** Convert a 160-bit Currency value to its canonical string representation.
 *
 *  Applies a priority-ordered decision tree:
 *  1. Zero value → `"XRP"` (the native asset).
 *  2. `noCurrency()` → `"1"`.
 *  3. All non-ISO bytes zero and three-character code in `kISO_CHAR_SET`
 *     and not equal to `"XRP"` → the three-character string.
 *  4. Everything else → a 40-character uppercase hex string.
 *
 *  @param currency The currency value to convert.
 *  @return A string in one of the four forms described above.
 *  @note A currency whose ISO position contains `"XRP"` but whose
 *      surrounding bytes are not all zero is returned as hex, making the
 *      anomaly visible rather than silently aliasing to the native currency.
 */
std::string
to_string(Currency const& currency)
{
    if (currency == beast::kZERO)
        return systemCurrencyCode();

    if (currency == noCurrency())
        return "1";

    static constexpr Currency kS_ISO_BITS("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

    if ((currency & kS_ISO_BITS).isZero())
    {
        std::string const iso(
            currency.data() + detail::kISO_CODE_OFFSET,
            currency.data() + detail::kISO_CODE_OFFSET + detail::kISO_CODE_LENGTH);

        // Specifying the system currency code using ISO-style representation
        // is not allowed.
        if ((iso != systemCurrencyCode()) &&
            (iso.find_first_not_of(detail::kISO_CHAR_SET) == std::string::npos))
        {
            return iso;
        }
    }

    return strHex(currency);
}

/** Parse a string into a Currency value, reporting success via return value.
 *
 *  Accepted forms:
 *  - Empty string or `"XRP"` → sets `currency` to zero (`xrpCurrency()`).
 *  - Exactly 3 characters from `kISO_CHAR_SET` → zero-pads and writes the
 *    code at byte offset 12 of `currency`.
 *  - 40-character hex string → parsed directly into `currency`.
 *  - Anything else → returns `false` and leaves `currency` unmodified.
 *
 *  @param currency Output parameter set on success.
 *  @param code The string to parse.
 *  @return `true` if parsing succeeded, `false` otherwise.
 *  @note Returns `true` even when the result is `badCurrency()`. This
 *      legacy behaviour is preserved to avoid breaking existing callers;
 *      use the return value (not the currency value) to detect failure.
 */
bool
toCurrency(Currency& currency, std::string const& code)
{
    if (code.empty() || (code.compare(systemCurrencyCode()) == 0))
    {
        currency = beast::kZERO;
        return true;
    }

    // Handle ISO-4217-like 3-digit character codes.
    if (code.size() == detail::kISO_CODE_LENGTH)
    {
        if (code.find_first_not_of(detail::kISO_CHAR_SET) != std::string::npos)
            return false;

        currency = beast::kZERO;

        std::ranges::copy(code, currency.begin() + detail::kISO_CODE_OFFSET);

        return true;
    }

    return currency.parseHex(code);
}

/** Parse a string into a Currency value, returning `noCurrency()` on failure.
 *
 *  Convenience wrapper around `toCurrency(Currency&, std::string const&)`.
 *  Prefer the two-argument overload when the caller must distinguish a parse
 *  failure from a legitimately absent currency.
 *
 *  @param code The string to parse.
 *  @return The parsed `Currency`, or `noCurrency()` if the string is invalid.
 *  @note Can return `badCurrency()` for hex input that encodes that sentinel.
 *      This legacy behaviour is preserved; callers should validate the result
 *      if they need to exclude `badCurrency()`.
 */
Currency
toCurrency(std::string const& code)
{
    Currency currency;
    if (!toCurrency(currency, code))
        currency = noCurrency();
    return currency;
}

/** The canonical representation of XRP, the ledger's native asset.
 *
 *  Returns the all-zeros `Currency` value (`beast::kZERO`). This is the
 *  protocol-defined encoding for XRP; `isXRP()` tests against this value.
 *  The returned reference is to a function-local static and is valid for
 *  the lifetime of the process.
 *
 *  @return A reference to the singleton zero `Currency`.
 */
Currency const&
xrpCurrency()
{
    static Currency const kCURRENCY(beast::kZERO);
    return kCURRENCY;
}

/** A placeholder currency used when no currency is specified.
 *
 *  Holds the value `1`. Data structures that require a `Currency` slot but
 *  have no meaningful currency to store use this sentinel. The returned
 *  reference is to a function-local static valid for the process lifetime.
 *
 *  @return A reference to the singleton `Currency{1}`.
 */
Currency const&
noCurrency()
{
    static Currency const kCURRENCY(1);
    return kCURRENCY;
}

/** A poisoned sentinel that marks the "XRP as ISO code" misuse.
 *
 *  Early adopters sometimes encoded XRP as the three-letter ISO code
 *  `"XRP"` (yielding value `0x5852500000000000`) rather than using the
 *  correct all-zeros form. This sentinel exists so that code paths that
 *  encounter such values can detect and reject them explicitly rather than
 *  silently treating them as valid currency. `to_string()` on this value
 *  produces a hex string, not `"XRP"`. The returned reference is to a
 *  function-local static valid for the process lifetime.
 *
 *  @return A reference to the singleton `Currency{0x5852500000000000}`.
 */
Currency const&
badCurrency()
{
    static Currency const kCURRENCY(0x5852500000000000);
    return kCURRENCY;
}

}  // namespace xrpl
