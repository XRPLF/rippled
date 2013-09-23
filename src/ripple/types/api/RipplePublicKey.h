//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEPUBLICKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPUBLICKEY_H_INCLUDED

#include "RippleCryptoIdentifier.h"

namespace ripple {

class RipplePublicKeyTraits
    : public RippleCryptoIdentifier <33, 28, true>
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
};

typedef CryptoIdentifierType <RipplePublicKeyTraits> RipplePublicKey;

}

#endif
