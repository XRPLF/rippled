//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEPRIVATEKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPRIVATEKEY_H_INCLUDED

#include "RippleCryptoIdentifier.h"

namespace ripple {

class RipplePrivateKeyTraits
    : public RippleCryptoIdentifier <32, 32, true>
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

typedef CryptoIdentifierType <RipplePrivateKeyTraits> RipplePrivateKey;

}

#endif
