#pragma once

#include <secp256k1.h>

namespace xrpl {

template <class = void>
secp256k1_context const*
secp256k1Context()
{
    struct holder
    {
        secp256k1_context* impl;
        // SECP256K1_CONTEXT_SIGN and SECP256K1_CONTEXT_VERIFY were
        // deprecated. All contexts support both signing and verification, so
        // SECP256K1_CONTEXT_NONE is the correct flag to use.
        holder() : impl(secp256k1_context_create(SECP256K1_CONTEXT_NONE))
        {
        }

        ~holder()
        {
            secp256k1_context_destroy(impl);
        }
    };
    static holder const h;
    return h.impl;
}

}  // namespace xrpl
