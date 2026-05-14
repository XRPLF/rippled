/** @file
 *  Process-wide singleton accessor for the libsecp256k1 context.
 *
 *  This header is an implementation detail of the XRPL protocol layer.
 *  External code should use `SecretKey` and `PublicKey` rather than
 *  including this header directly.
 */

#pragma once

#include <secp256k1.h>

namespace xrpl {

/** Returns the process-wide, read-only libsecp256k1 context.
 *
 *  The context is created on the first call with both
 *  `SECP256K1_CONTEXT_VERIFY` and `SECP256K1_CONTEXT_SIGN` flags so that
 *  the same instance can service signing (in `SecretKey.cpp`) and
 *  verification (in `PublicKey.cpp`) without any additional setup. It is
 *  destroyed when the process exits.
 *
 *  The function template with a defaulted, unused type parameter is a
 *  standard C++ idiom for defining a function that owns a `static` local
 *  variable in a header file without violating the One Definition Rule
 *  (ODR). The `static Holder` is initialized exactly once regardless of
 *  how many translation units include this header.
 *
 *  @tparam Unused Defaulted to `void`; callers should never supply a
 *      value. The parameter exists solely to satisfy the ODR.
 *  @return A `const` pointer to the shared `secp256k1_context`. The
 *      `const` qualifier matches the secp256k1 library's thread-safety
 *      contract: concurrent reads through a `const*` require no locking.
 *  @note Do not call `secp256k1_context_destroy` on the returned pointer;
 *      lifetime is managed internally by the `Holder` RAII wrapper.
 */
template <class = void>
secp256k1_context const*
secp256k1Context()
{
    /** RAII owner for the shared `secp256k1_context`.
     *
     *  Allocates the context with both `SECP256K1_CONTEXT_VERIFY` and
     *  `SECP256K1_CONTEXT_SIGN` on construction, and destroys it on
     *  destruction. The single `static` instance inside
     *  `secp256k1Context()` lives for the duration of the process.
     */
    struct Holder
    {
        secp256k1_context* impl;
        Holder() : impl(secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN))
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
