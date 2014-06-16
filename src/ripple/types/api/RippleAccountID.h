//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TYPES_RIPPLEACCOUNTID_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEACCOUNTID_H_INCLUDED

#include <ripple/types/api/CryptoIdentifier.h>
#include <ripple/types/api/IdentifierType.h>

#include <array>

namespace ripple {

class RippleAccountIDTraits
    : public CryptoIdentifier <20, 0, true>
{
public:
    template <typename Other>
    struct assign
    {
        void operator() (value_type& value, Other const& other)
        {
            value = other;
        }
    };

    /** Convert to std::string.
    */
    // VFALCO TODO Cache results in an associative map, to replicated
    //             the optimization performed in RippledAddress.cp
    //
    static std::string to_string (value_type const& value)
    {
        value_type::storage_type const& storage (value.storage());
        // We will convert to little endian with an extra pad byte
        std::array <std::uint8_t, value_type::storage_size + 1> le;
        std::reverse_copy (storage.begin(), storage.end(), le.begin());
        // Set pad byte zero to make BIGNUM always positive
        le.back() = 0;
        return Base58::raw_encode (le.data(), le.data() + le.size(),
            Base58::getRippleAlphabet(), checked);
    }
};

typedef IdentifierType <RippleAccountIDTraits> RippleAccountID;

}

#endif
