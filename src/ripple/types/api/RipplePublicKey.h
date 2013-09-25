//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEPUBLICKEY_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPUBLICKEY_H_INCLUDED

namespace ripple {

class RipplePublicKeyTraits
    : public CryptoIdentifier <33, 28, true>
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

typedef IdentifierType <RipplePublicKeyTraits> RipplePublicKey;

}

#endif
