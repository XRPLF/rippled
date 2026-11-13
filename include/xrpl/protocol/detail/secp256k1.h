#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>

#include <secp256k1.h>

#include <array>
#include <string>

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
    // The actual context is held in a magic static: it is constructed exactly
    // once, on first use, and used by every caller.
    //
    // The context is intentionally never destroyed and its memory will not be
    // released for the lifetime of the process. Some (very smart) tools might
    // flag this as a memory leak; it is not.
    static secp256k1_context const* const kContext = [] {
        auto* const ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

        if (ctx == nullptr)
            logicError("secp256k1_context_create failed");

        // libsecp256k1 reports two kinds of failure through per-context
        // callbacks:
        //
        // 1. illegal arguments, when API preconditions are violated; and
        // 2. internal errors, when internal consistency/sanity checks fail.
        //
        // The default handlers print to stderr and terminate the process. We
        // install our own handlers which invoke logicError. There is nothing
        // else we can do to recover.
        secp256k1_context_set_illegal_callback(
            ctx,
            [](char const* msg, void*) noexcept {
                logicError(std::string{"secp256k1 illegal argument: "} + msg);
            },
            nullptr);

        secp256k1_context_set_error_callback(
            ctx,
            [](char const* msg, void*) noexcept {
                logicError(std::string{"secp256k1 error: "} + msg);
            },
            nullptr);

        // Blind the generator multiplication used by signing and key derivation
        // against side-channel leakage. This is strongly recommended when using
        // secret keys.
        std::array<unsigned char, 32> seed{};

        beast::rngfill(seed, cryptoPrng());

        if (secp256k1_context_randomize(ctx, seed.data()) != 1)
            logicError("secp256k1_context_randomize failed");

        secureErase(seed.data(), seed.size());

        return ctx;
    }();

    return kContext;
}

}  // namespace xrpl
