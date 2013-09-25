//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEACCOUNTID_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEACCOUNTID_H_INCLUDED

#include "CryptoIdentifier.h"

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
        FixedArray <uint8, value_type::storage_size + 1> le;
        std::reverse_copy (storage.begin(), storage.end(), le.begin());
        // Set pad byte zero to make BIGNUM always positive
        le.back() = 0;
        return Base58::raw_encode (le.begin(), le.end(),
            Base58::getRippleAlphabet(), checked);
    }
};

typedef IdentifierType <RippleAccountIDTraits> RippleAccountID;

}

#endif
