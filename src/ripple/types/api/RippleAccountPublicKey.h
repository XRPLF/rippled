//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEACCOUNTPUBLICKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEACCOUNTPUBLICKEY_H_INCLUDED

#include "RippleCryptoIdentifier.h"

namespace ripple {

class RippleAccountPublicKeyTraits
    : public RippleCryptoIdentifier <33, 35, true>
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

typedef CryptoIdentifierType <RippleAccountPublicKeyTraits> RippleAccountPublicKey;

}

#endif
