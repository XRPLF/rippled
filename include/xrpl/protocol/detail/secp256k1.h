#pragma once

#include <secp256k1.h>

namespace xrpl {

template <class = void>
secp256k1_context const*
secp256k1Context()
{
    struct Holder
    {
        secp256k1_context* impl;
        // SECP256K1_CONTEXT_SIGN and SECP256K1_CONTEXT_VERIFY were deprecated.
        // All contexts support both signing and verification, so
        // SECP256K1_CONTEXT_NONE is the correct flag to use.
        Holder() : impl(secp256k1_context_create(SECP256K1_CONTEXT_NONE))
        {
        }

        ~Holder()
        {
            secp256k1_context_destroy(impl);
        }
    };
    static Holder const kH;
    return kH.impl;
}

}  // namespace xrpl
