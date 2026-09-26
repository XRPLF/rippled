#include <xrpl/protocol/UintTypes.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/SystemParameters.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace xrpl {

// For details on the protocol-level serialization please visit
// https://xrpl.org/serialization.html#currency-codes

namespace detail {

// Characters we are willing to allow in the ASCII representation of a
// three-letter currency code.
constexpr std::string_view kIsoCharSet =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "<>(){}[]|?!@#$%^&*";

// The location (in bytes) of the 3 digit currency inside a 160-bit value
constexpr std::size_t kIsoCodeOffset = 12;

// The length of an ISO-4217 like code
constexpr std::size_t kIsoCodeLength = 3;

}  // namespace detail

std::string
to_string(Currency const& currency)
{
    if (currency == xrpCurrency())
        return systemCurrencyCode();

    if (currency == noCurrency())
        return "1";

    static constexpr Currency kSIsoBits("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

    if ((currency & kSIsoBits).isZero())
    {
        std::string const iso(
            currency.data() + detail::kIsoCodeOffset,
            currency.data() + detail::kIsoCodeOffset + detail::kIsoCodeLength);

        // Specifying the system currency code using ISO-style representation
        // is not allowed.
        if ((iso != systemCurrencyCode()) &&
            (iso.find_first_not_of(detail::kIsoCharSet) == std::string::npos))
        {
            return iso;
        }
    }

    return strHex(currency);
}

bool
toCurrency(Currency& currency, std::string_view code)
{
    if (code.empty() || code == systemCurrencyCode())
    {
        currency = beast::kZero;
        return true;
    }

    // Handle ISO-4217-like 3-digit character codes.
    if (code.size() == detail::kIsoCodeLength)
    {
        if (code.find_first_not_of(detail::kIsoCharSet) != std::string::npos)
            return false;

        currency = beast::kZero;

        std::ranges::copy(code, currency.begin() + detail::kIsoCodeOffset);

        return true;
    }

    return currency.parseHex(code);
}

Currency
toCurrency(std::string_view code)
{
    Currency currency;
    if (!toCurrency(currency, code))
        currency = noCurrency();
    return currency;
}

}  // namespace xrpl
