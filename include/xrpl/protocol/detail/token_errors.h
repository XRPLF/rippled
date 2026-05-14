/** @file
 *  Error taxonomy for the XRPL Base58Check token codec.
 *
 *  Defines `TokenCodecErrc` and wires it into the `std::error_code`
 *  machinery so that codec failures compose cleanly with `B58Result<T>`
 *  (`Expected<T, std::error_code>`) without exceptions or casts.
 */

#pragma once

#include <system_error>

namespace xrpl {

/** Error codes produced by the Base58Check token codec.
 *
 *  Every distinct failure mode the encode/decode pipeline can signal is
 *  represented here. `Success` is zero so that a default-constructed
 *  `std::error_code` built from this enum compares equal to no-error.
 *
 *  The enumerators fall into four groups:
 *  - **Size** (`InputTooLarge`, `InputTooSmall`, `OutputTooSmall`): bounds
 *    violations detected before or during a conversion pass.
 *  - **Alphabet** (`BadB58Character`, `InvalidEncodingChar`): invalid bytes
 *    in a Base58 string, distinguished by context — a character outside the
 *    58-symbol alphabet vs. one that is invalid for the specific encoding.
 *  - **Semantic** (`MismatchedTokenType`, `MismatchedChecksum`): structurally
 *    valid input that fails XRPL-specific validation — wrong one-byte type
 *    prefix or a four-byte checksum mismatch.
 *  - **Arithmetic** (`OverflowAdd`): bignum addition overflow during the
 *    multi-precision fast-path decode, indicating the encoded value exceeds
 *    the expected output width. Valid Base58Check inputs never trigger this;
 *    it surfaces only on malformed or adversarial input.
 */
enum class TokenCodecErrc {
    Success = 0,           /**< Conversion succeeded. Zero value is the
                                `std::error_code` no-error sentinel. */
    InputTooLarge,         /**< Input buffer exceeds the maximum accepted size. */
    InputTooSmall,         /**< Input buffer is shorter than the minimum required. */
    BadB58Character,       /**< A byte in the input is not in the Base58 alphabet. */
    OutputTooSmall,        /**< Caller-supplied output buffer is too small for the result. */
    MismatchedTokenType,   /**< Decoded one-byte token-type prefix does not match
                                the expected `TokenType`. */
    MismatchedChecksum,    /**< Decoded four-byte checksum does not match the
                                recomputed value; the token is corrupt or truncated. */
    InvalidEncodingChar,   /**< A character is syntactically invalid for this
                                encoding context (distinct from `BadB58Character`). */
    OverflowAdd,           /**< Bignum addition overflowed the fixed-size limb array;
                                the encoded value is too large for the output type. */
    Unknown,               /**< Catch-all for unrecognised internal error states. */
};

}  // namespace xrpl

namespace std {

/** Opt `TokenCodecErrc` into the `std::error_code` implicit-conversion protocol.
 *
 *  With this specialisation a bare `TokenCodecErrc` enumerator can be stored
 *  in or compared against a `std::error_code` without an explicit
 *  `make_error_code` call, enabling natural use in `Unexpected(errc)` and
 *  `ec == TokenCodecErrc::X` comparisons.
 */
template <>
struct is_error_code_enum<xrpl::TokenCodecErrc> : true_type
{
};

}  // namespace std

namespace xrpl {
namespace detail {

/** `std::error_category` singleton for the Base58Check codec error domain.
 *
 *  Provides the stable category name `"TokenCodecError"` and human-readable
 *  messages for every `TokenCodecErrc` enumerator. Declared `final` — only
 *  one category exists for this error domain.
 *
 *  Obtain the singleton via `tokenCodecErrcCategory()` rather than
 *  constructing this class directly.
 */
class TokenCodecErrcCategory : public std::error_category
{
public:
    /** Return the stable name of this error category. */
    [[nodiscard]] char const*
    name() const noexcept final
    {
        return "TokenCodecError";
    }

    /** Translate a `TokenCodecErrc` integer value into a human-readable string.
     *
     *  @param c An integer cast of a `TokenCodecErrc` enumerator.
     *  @return A short English description of the error condition; returns
     *      `"unknown"` for unrecognised values.
     */
    [[nodiscard]] std::string
    message(int c) const final
    {
        switch (static_cast<TokenCodecErrc>(c))
        {
            case TokenCodecErrc::Success:
                return "conversion successful";
            case TokenCodecErrc::InputTooLarge:
                return "input too large";
            case TokenCodecErrc::InputTooSmall:
                return "input too small";
            case TokenCodecErrc::BadB58Character:
                return "bad base 58 character";
            case TokenCodecErrc::OutputTooSmall:
                return "output too small";
            case TokenCodecErrc::MismatchedTokenType:
                return "mismatched token type";
            case TokenCodecErrc::MismatchedChecksum:
                return "mismatched checksum";
            case TokenCodecErrc::InvalidEncodingChar:
                return "invalid encoding char";
            case TokenCodecErrc::Unknown:
                return "unknown";
            default:
                return "unknown";
        }
    }
};

}  // namespace detail

/** Return the `TokenCodecErrc` error category singleton.
 *
 *  Uses a function-local static to guarantee thread-safe, zero-overhead
 *  initialisation (C++11 magic-static semantics). Called by
 *  `make_error_code` and by the implicit `std::error_code` conversion
 *  enabled through `is_error_code_enum`.
 *
 *  @return A `const` reference to the single `TokenCodecErrcCategory` instance.
 */
inline xrpl::detail::TokenCodecErrcCategory const&
tokenCodecErrcCategory()
{
    static xrpl::detail::TokenCodecErrcCategory const kC;
    return kC;
}

/** Construct a `std::error_code` from a `TokenCodecErrc` enumerator.
 *
 *  This is the ADL-located overload required by the `std::error_code`
 *  implicit-conversion protocol. Combined with the `is_error_code_enum`
 *  specialisation, it allows `TokenCodecErrc` values to be returned
 *  directly wherever a `std::error_code` is expected — including inside
 *  `Unexpected(errc)` expressions in `B58Result<T>` return sites.
 *
 *  @param e The codec error enumerator to wrap.
 *  @return A `std::error_code` pairing `e` with `tokenCodecErrcCategory()`.
 */
inline std::error_code
make_error_code(xrpl::TokenCodecErrc e)
{
    return {static_cast<int>(e), tokenCodecErrcCategory()};
}

}  // namespace xrpl
