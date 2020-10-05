//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/beast/utility/Zero.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <string_view>

namespace ripple {

// For details on the protocol-level serialization please visit
// https://xrpl.org/serialization.html#currency-codes

namespace detail {

// Characters we are willing to allow in the ASCII representation of a
// three-letter currency code.
constexpr std::string_view isoCharSet =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "<>(){}[]|?!@#$%^&*";

// The location (in bytes) of the 3 digit currency inside a 160-bit value
constexpr std::size_t isoCodeOffset = 12;

// The length of an ISO-4217 like code
constexpr std::size_t isoCodeLength = 3;

}  // namespace detail

std::string
to_string(Currency const& currency)
{
    if (currency == beast::zero)
        return systemCurrencyCode();

    if (currency == noCurrency())
        return "1";

    static Currency const sIsoBits = []() {
        Currency c;
        (void)c.parseHex("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");
        return c;
    }();

    if ((currency & sIsoBits).isZero())
    {
        std::string const iso(
            currency.data() + detail::isoCodeOffset,
            currency.data() + detail::isoCodeOffset + detail::isoCodeLength);

        // Specifying the system currency code using ISO-style representation
        // is not allowed.
        if ((iso != systemCurrencyCode()) &&
            (iso.find_first_not_of(detail::isoCharSet) == std::string::npos))
        {
            return iso;
        }
    }

    return strHex(currency);
}

bool
to_currency(Currency& currency, std::string const& code)
{
    if (code.empty() || !code.compare(systemCurrencyCode()))
    {
        currency = beast::zero;
        return true;
    }

    // Handle ISO-4217-like 3-digit character codes.
    if (code.size() == detail::isoCodeLength)
    {
        if (code.find_first_not_of(detail::isoCharSet) != std::string::npos)
            return false;

        currency = beast::zero;

        std::transform(
            code.begin(),
            code.end(),
            currency.begin() + detail::isoCodeOffset,
            [](auto c) {
                return static_cast<unsigned char>(
                    ::toupper(static_cast<unsigned char>(c)));
            });

        return true;
    }

    return currency.parseHex(code);
}

Currency
to_currency(std::string const& code)
{
    Currency currency;
    if (!to_currency(currency, code))
        currency = noCurrency();
    return currency;
}

Currency const&
xrpCurrency()
{
    static Currency const currency(beast::zero);
    return currency;
}

Currency const&
noCurrency()
{
    static Currency const currency(1);
    return currency;
}

Currency const&
badCurrency()
{
    static Currency const currency(0x5852500000000000);
    return currency;
}

}  // namespace ripple
