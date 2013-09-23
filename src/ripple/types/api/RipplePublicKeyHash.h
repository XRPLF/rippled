//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLEPUBLICKEYHASH_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPUBLICKEYHASH_H_INCLUDED

namespace ripple {

/** Traits for the public key hash. */
class RipplePublicKeyHashTraits : public SimpleIdentifier <20>
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

/** A container holding the 160-bit hash of the 257-bit public key. */
typedef IdentifierType <RipplePublicKeyHashTraits> RipplePublicKeyHash;

}

#endif

