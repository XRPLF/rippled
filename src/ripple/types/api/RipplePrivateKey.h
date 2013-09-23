//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEPRIVATEKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPRIVATEKEY_H_INCLUDED

#include "CryptoIdentifier.h"

namespace ripple {

class RipplePrivateKeyTraits
    : public CryptoIdentifier <32, 32, true>
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

typedef IdentifierType <RipplePrivateKeyTraits> RipplePrivateKey;

}

#endif
