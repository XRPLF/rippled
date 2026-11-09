#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>

#include <secp256k1.h>

#include <array>
#include <cstdint>

namespace xrpl {

/**
 * The process-wide libsecp256k1 context.
 *
 * This is created and randomized exactly once, on first use, and used by
 * every caller.
 */
[[nodiscard]] inline secp256k1_context const*
secp256k1Context()
{
    static secp256k1_context const* const ctx = [] {
        auto* const ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

        if (ctx == nullptr)
            logicError("secp256k1_context_create failed");

        // Blind the generator multiplication used by signing and key derivation
        // against side-channel leakage. This is strongly recommended when using
        // secret keys.
        std::array<std::uint8_t, 32> seed;

        beast::rngfill(seed, cryptoPrng());

        if (secp256k1_context_randomize(ctx, seed.data()) != 1)
            logicError("secp256k1_context_randomize failed");

        secureErase(seed.data(), seed.size());

        return ctx;
    }();

    return ctx;
}

}  // namespace xrpl
