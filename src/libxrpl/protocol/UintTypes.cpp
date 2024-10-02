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

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/UintTypes.h>
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

    static constexpr Currency sIsoBits(
        "FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

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

        std::copy(
            code.begin(), code.end(), currency.begin() + detail::isoCodeOffset);

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

bool
validCurrency(Currency const& currency, PaymentTx paymentTx)
{
    // Allow payments for invalid non-standard currencies
    // created pre fixNonStandardCurrency amendment
    static std::set<Currency> whiteList = {
        Currency{"0000000000000000000000000000000078415059"},
        Currency{"00000000004150756E6B30310000000000000000"},
        Currency{"0000000000D9A928EFBCBEE297A1EFBCBE29DBB6"},
        Currency{"0000000000414C6F676F30330000000000000000"},
        Currency{"0000000000000000000000005852500000000000"},
        Currency{"000028E0B2A05FE0B2A029E2948CE288A9E29490"},
        Currency{"00000028E2989EEFBE9FE28880EFBE9F29E2989E"},
        Currency{"00000028E381A3E29795E280BFE2979529E381A3"},
        Currency{"000000000000005C6D2F5F283E5F3C295F5C6D2F"},
        Currency{"00000028E295AFC2B0E296A1C2B0EFBC89E295AF"},
        Currency{"0000000000000000000000005852527570656500"},
        Currency{"000000000000000000000000302E310000000000"},
        Currency{"0000000000E1839A28E0B2A05FE0B2A0E1839A29"},
        Currency{"0000000048617070794E6577596561725852504C"},
        Currency{"0000E29D9AE29688E29590E29590E29688E29D9A"},
        Currency{"000028E297A35FE297A229E2948CE288A9E29490"},
        Currency{"00000000CA95E0B2A0E0B2BFE1B4A5E0B2A0CA94"},
        Currency{"000000282D5F282D5F282D5F2D295F2D295F2D29"},
        Currency{"0000000000000000000000000000000078415049"},
        Currency{"00000000000028E295ADE0B2B05FE280A2CC8129"}};
    static constexpr Currency sIsoBits(
        "FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

    // XRP or standard currency
    if (currency == xrpCurrency() || ((currency & sIsoBits).isZero()))
        return true;

    // Non-standard currency must not start with 0x00
    if (*currency.cbegin() != 0x00)
        return true;

    return paymentTx == PaymentTx::Yes && whiteList.contains(currency);
}

}  // namespace ripple
