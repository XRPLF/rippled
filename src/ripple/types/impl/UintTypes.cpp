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

#include <ripple/types/api/UintTypes.h>

#include <ripple/module/data/protocol/RippleAddress.h>
#include <ripple/module/data/protocol/SerializedTypes.h>

namespace ripple {

std::string to_string(Account const& account)
{
    return RippleAddress::createAccountID (account).humanAccountID ();
}

std::string to_string(Currency const& currency)
{
    static Currency const sIsoBits ("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

    // Characters we are willing to include the ASCII representation
    // of a three-letter currency code
    static std::string legalASCIICurrencyCharacters =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "<>(){}[]|?!@#$%^&*";

    if (currency == zero)
        return systemCurrencyCode();

    if (currency == noCurrency())
        return "1";

    if ((currency & sIsoBits).isZero ())
    {
        // The offset of the 3 character ISO code in the currency descriptor
        int const isoOffset = 12;

        std::string const iso(
            currency.data () + isoOffset,
            currency.data () + isoOffset + 3);

        // Specifying the system currency code using ISO-style representation
        // is not allowed.
        if ((iso != systemCurrencyCode()) &&
            (iso.find_first_not_of (legalASCIICurrencyCharacters) == std::string::npos))
        {
            return iso;
        }
    }

    uint160 simple;
    simple.copyFrom(currency);

    return to_string(simple);
}

bool to_currency(Currency& currency, std::string const& code)
{
    if (code.empty () || !code.compare (systemCurrencyCode()))
    {
        currency = zero;
        return true;
    }

    static const int CURRENCY_CODE_LENGTH = 3;
    if (code.size () == CURRENCY_CODE_LENGTH)
    {
        Blob codeBlob (CURRENCY_CODE_LENGTH);

        std::transform (code.begin (), code.end (), codeBlob.begin (), ::toupper);

        Serializer  s;

        s.addZeros (96 / 8);
        s.addRaw (codeBlob);
        s.addZeros (16 / 8);
        s.addZeros (24 / 8);

        s.get<160> (currency, 0);
        return true;
    }

    if (40 == code.size ())
        return currency.SetHex (code);

    return false;
}

Currency to_currency(std::string const& code)
{
    Currency currency;
    if (!to_currency(currency, code))
        currency = noCurrency();
    return currency;
}

bool to_issuer(Account& issuer, std::string const& s)
{
    if (s.size () == (160 / 4))
    {
        issuer.SetHex (s);
        return true;
    }
    RippleAddress address;
    bool success = address.setAccountID (s);
    if (success)
        issuer = address.getAccountID ();
    return success;
}

const char* systemCurrencyCode() {
    return "XRP";
}

Account const& xrpAccount()
{
    static const Account account(0);
    return account;
}

Currency const& xrpCurrency()
{
    static const Currency currency(0);
    return currency;
}

Account const& noAccount()
{
    static const Account account(1);
    return account;
}

Currency const& noCurrency()
{
    static const Currency currency(1);
    return currency;
}

Currency const& badCurrency()
{
    static const Currency currency(0x5852500000000000);
    return currency;
}

} // ripple
