//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEACCOUNTPUBLICKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEACCOUNTPUBLICKEY_H_INCLUDED

#include "CryptoIdentifier.h"

namespace ripple {

class RippleAccountPublicKeyTraits
    : public CryptoIdentifier <33, 35, true>
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

typedef IdentifierType <RippleAccountPublicKeyTraits> RippleAccountPublicKey;

}

#endif
