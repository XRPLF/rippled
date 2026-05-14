/** @file
 *  Public interface for Base58Check encoding and decoding of XRPL
 *  cryptographic identifiers.
 *
 *  Raw byte sequences — account IDs, node keys, seeds — are converted to and
 *  from the human-readable strings that appear in XRPL transactions and client
 *  APIs (e.g., `rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh` for an account address).
 *
 *  Every encoded token follows the wire layout:
 *  @code
 *    [ type byte (1) ][ raw payload (N) ][ checksum (4) ]
 *  @endcode
 *  where the checksum is the first four bytes of SHA-256(SHA-256(type ‖ payload)).
 *
 *  Three namespaces expose encoding/decoding:
 *  - `xrpl::` — public API; dispatches to `b58_fast` on GCC/Clang, `b58_ref`
 *      on MSVC.
 *  - `xrpl::b58_ref` — portable O(n²) reference implementation (all compilers).
 *  - `xrpl::b58_fast` — 10–15× faster implementation using GCC's
 *      `unsigned __int128`; guarded by `#ifndef _MSC_VER`.
 *
 *  @see https://xrpl.org/base58-encodings.html
 */

#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/detail/token_errors.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace xrpl {

/** Result type for fallible Base58Check codec operations.
 *
 *  Holds either a value of type `T` on success, or a `std::error_code`
 *  (wrapping a `TokenCodecErrc` enumerator) on failure.  Marked
 *  `[[nodiscard]]` via `Expected` so callers cannot silently ignore errors.
 *
 *  @tparam T The success-path value type (e.g., `std::span<std::uint8_t>`).
 *  @see TokenCodecErrc
 */
template <class T>
using B58Result = Expected<T, std::error_code>;

/** One-byte version prefixes for each XRPL identifier category.
 *
 *  The prefix byte is prepended to the raw payload before Base58Check
 *  encoding.  It causes the resulting string to begin with a
 *  category-specific letter in the XRPL alphabet — e.g., account addresses
 *  start with `'r'` and family seeds start with `'s'` — providing a
 *  recognisable visual cue without decoding.
 *
 *  `None` and `FamilyGenerator` are reserved and currently unused; their
 *  numeric values must not be reassigned to new token categories.
 *
 *  @note These values are protocol-stable and must never be changed.  All
 *      Base58Check-encoded XRPL identifiers in existence carry one of these
 *      bytes as their first decoded byte.
 */
enum class TokenType : std::uint8_t {
    None = 1,            ///< Reserved; unused.
    NodePublic = 28,     ///< Validator/peer public key.
    NodePrivate = 32,    ///< Validator/peer private key.
    AccountID = 0,       ///< Classic account address (20-byte hash).
    AccountPublic = 35,  ///< Account public key.
    AccountSecret = 34,  ///< Account private key.
    FamilyGenerator = 41, ///< Reserved; unused.
    FamilySeed = 33      ///< Key-generation seed (16 bytes).
};

/** Decode a Base58Check string into a typed XRPL value.
 *
 *  The overload with no explicit `TokenType` is used when the target type
 *  `T` carries its own implicit token type (e.g., `parseBase58<AccountID>`).
 *  The overload that accepts a `TokenType` is used when the type byte must
 *  be supplied by the caller (e.g., `parseBase58<PublicKey>(TokenType::NodePublic, s)`).
 *
 *  No definition is provided in this header.  Explicit template
 *  specialisations for each concrete XRPL type live alongside that type's
 *  own implementation (e.g., `AccountID.cpp`, `PublicKey.cpp`).
 *
 *  @tparam T The target XRPL type to parse into.
 *  @param s The Base58Check-encoded string to decode.
 *  @return The decoded value, or `std::nullopt` if the string is invalid,
 *      has the wrong token type, or fails its checksum.
 */
template <class T>
[[nodiscard]] std::optional<T>
parseBase58(std::string const& s);

/** Decode a Base58Check string with an explicit token type into a typed XRPL value.
 *
 *  @tparam T The target XRPL type to parse into.
 *  @param type The expected one-byte token-type prefix.  Decoding fails if
 *      the decoded prefix does not match.
 *  @param s The Base58Check-encoded string to decode.
 *  @return The decoded value, or `std::nullopt` on any error.
 */
template <class T>
[[nodiscard]] std::optional<T>
parseBase58(TokenType type, std::string const& s);

/** Encode data in Base58Check format using the XRPL alphabet.
 *
 *  Prepends `type` as a one-byte version prefix, appends a 4-byte
 *  SHA-256(SHA-256(type ‖ data)) checksum, then Base58-encodes the
 *  concatenation.
 *
 *  On non-MSVC platforms this dispatches to `b58_fast::encodeBase58Token`;
 *  on MSVC it falls back to `b58_ref::encodeBase58Token`.
 *
 *  @param type The token category prefix byte.
 *  @param token Pointer to the raw payload bytes.
 *  @param size Number of payload bytes.
 *  @return The Base58Check-encoded string.
 *
 *  @see https://xrpl.org/base58-encodings.html
 */
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

/** Decode a Base58Check string and validate its token type and checksum.
 *
 *  Returns the raw payload bytes (without the type prefix or checksum) as a
 *  string, or an empty string if the input is malformed, the token type does
 *  not match `type`, or the checksum fails.
 *
 *  On non-MSVC platforms this dispatches to `b58_fast::decodeBase58Token`;
 *  on MSVC it falls back to `b58_ref::decodeBase58Token`.
 *
 *  @param s The Base58Check-encoded string to decode.
 *  @param type The expected one-byte token-type prefix.
 *  @return The raw decoded payload, or an empty string on any error.
 *  @note Error detail is lost on failure; prefer the span-based
 *      `b58_fast::decodeBase58Token` overload where typed errors matter.
 */
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

/** Portable O(n²) reference implementation of the XRPL Base58Check codec.
 *
 *  Adapted from Bitcoin Core.  Performs direct base-256 ↔ base-58 digit
 *  conversion without any compiler extensions, making it available on all
 *  platforms including MSVC.  For large payloads the O(n²) cost is
 *  measurable; prefer `b58_fast` where available.
 */
namespace b58_ref {

/** Encode data in Base58Check format using the XRPL alphabet.
 *
 *  Portable reference implementation; does not use `__int128` or other
 *  compiler extensions.
 *
 *  @param type The token category prefix byte.
 *  @param token Pointer to the raw payload bytes.
 *  @param size Number of payload bytes.
 *  @return The Base58Check-encoded string.
 */
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

/** Decode a Base58Check string and validate its token type and checksum.
 *
 *  Portable reference implementation.
 *
 *  @param s The Base58Check-encoded string to decode.
 *  @param type The expected one-byte token-type prefix.
 *  @return The raw decoded payload, or an empty string on any error.
 */
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

/** Raw base-conversion primitives, exposed for unit testing only.
 *
 *  These functions operate on byte spans without any XRPL framing (no
 *  token-type prefix and no checksum), allowing the numeric conversion to
 *  be verified in isolation.
 */
namespace detail {

/** Encode raw bytes into a Base58 string using the XRPL alphabet.
 *
 *  Performs the O(n²) base-256 → base-58 conversion.  No token-type prefix
 *  or checksum is applied.
 *
 *  @param message Pointer to the input bytes.
 *  @param size Number of input bytes.
 *  @param temp Caller-supplied scratch buffer; must be at least `size` bytes.
 *  @param tempSize Size of the scratch buffer in bytes.
 *  @return The Base58-encoded string.
 *  @note Exposed for unit testing only; production code should call
 *      `b58_ref::encodeBase58Token`.
 */
std::string
encodeBase58(void const* message, std::size_t size, void* temp, std::size_t tempSize);

/** Decode a raw Base58 string into bytes using the XRPL alphabet.
 *
 *  Performs the base-58 → base-256 conversion without validating any token
 *  prefix or checksum.
 *
 *  @param s The Base58-encoded string to decode.
 *  @return The decoded bytes, or an empty string if any character is outside
 *      the XRPL Base58 alphabet.
 *  @note Exposed for unit testing only; production code should call
 *      `b58_ref::decodeBase58Token`.
 */
std::string
decodeBase58(std::string const& s);

}  // namespace detail
}  // namespace b58_ref

#ifndef _MSC_VER
/** 10–15× faster Base58Check codec using GCC/Clang's `unsigned __int128`.
 *
 *  The algorithm routes through an intermediate base-58^10 representation.
 *  `58^10 = 430,804,206,899,405,824` fits in a 64-bit register, so groups of
 *  ten base-58 digits can be processed as a single 64-bit word.  The
 *  expensive multi-precision arithmetic is then performed on far fewer, larger
 *  coefficients.  The three-stage pipeline is:
 *  @code
 *    base 58 → base 58^10 → base 2^64 → base 2^8
 *  @endcode
 *  Conversions between bases that are powers of one another are trivial
 *  concatenations; only the middle hop requires multi-precision work.
 *
 *  The span-based overloads avoid heap allocation in the hot path.  The
 *  `std::string`-returning overloads are provided for API compatibility but
 *  each require one allocation.
 *
 *  @note Not available on MSVC, which lacks `unsigned __int128`.
 */
namespace b58_fast {

/** Encode data in Base58Check format into a caller-supplied buffer.
 *
 *  Prepends `tokenType` as a one-byte version prefix, appends a 4-byte
 *  checksum, and Base58-encodes the result into `out`.  No heap allocation.
 *
 *  @param tokenType The token category prefix byte.
 *  @param input The raw payload bytes to encode.
 *  @param out Buffer to receive the Base58Check-encoded bytes.  Must be
 *      large enough to hold the encoded output.
 *  @return On success, a sub-span of `out` covering the encoded bytes.
 *      On failure, a `std::error_code` wrapping a `TokenCodecErrc` value
 *      (e.g., `OutputTooSmall` if `out` is insufficient).
 */
[[nodiscard]] B58Result<std::span<std::uint8_t>>
encodeBase58Token(
    TokenType tokenType,
    std::span<std::uint8_t const> input,
    std::span<std::uint8_t> out);

/** Decode a Base58Check string into a caller-supplied buffer.
 *
 *  Validates the token-type prefix and 4-byte checksum before writing to
 *  `outBuf`.  No heap allocation.
 *
 *  @param type The expected one-byte token-type prefix.
 *  @param s The Base58Check-encoded string to decode.
 *  @param outBuf Buffer to receive the raw decoded payload bytes.  Must be
 *      large enough for the payload (payload size = decoded size − 5).
 *  @return On success, a sub-span of `outBuf` covering the decoded bytes.
 *      On failure, a `std::error_code` wrapping a `TokenCodecErrc`
 *      (e.g., `MismatchedTokenType`, `MismatchedChecksum`, `OutputTooSmall`,
 *      or `BadB58Character`).
 */
[[nodiscard]] B58Result<std::span<std::uint8_t>>
decodeBase58Token(TokenType type, std::string_view s, std::span<std::uint8_t> outBuf);

/** Encode data in Base58Check format, returning a `std::string`.
 *
 *  Legacy-compatible overload matching the `b58_ref` API.  Requires one
 *  heap allocation for the returned string.
 *
 *  @param type The token category prefix byte.
 *  @param token Pointer to the raw payload bytes.
 *  @param size Number of payload bytes.
 *  @return The Base58Check-encoded string.
 */
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

/** Decode a Base58Check string, returning a `std::string`.
 *
 *  Legacy-compatible overload matching the `b58_ref` API.  Requires one
 *  heap allocation for the returned string.  Error detail is lost on
 *  failure; prefer the span-based overload where typed errors matter.
 *
 *  @param s The Base58Check-encoded string to decode.
 *  @param type The expected one-byte token-type prefix.
 *  @return The raw decoded payload, or an empty string on any error.
 */
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

/** Raw base-conversion primitives, exposed for unit testing only.
 *
 *  These functions perform the numeric base conversion without any XRPL
 *  framing (no token-type prefix and no checksum), enabling isolated testing
 *  of the fast-path arithmetic.
 */
namespace detail {

/** Convert big-endian base-256 bytes to a big-endian Base58 byte sequence.
 *
 *  Uses the three-stage `b256 → b58^10 → b2^64 → b58` pipeline.  No token
 *  prefix or checksum is applied.
 *
 *  @param input The big-endian base-256 bytes to convert.
 *  @param out Buffer to receive the Base58-encoded bytes.
 *  @return On success, a sub-span of `out` covering the result.
 *      On failure, a `std::error_code` (e.g., `OutputTooSmall`).
 *  @note Exposed for unit testing only.
 */
B58Result<std::span<std::uint8_t>>
b256ToB58Be(std::span<std::uint8_t const> input, std::span<std::uint8_t> out);

/** Convert a big-endian Base58 byte sequence to big-endian base-256 bytes.
 *
 *  Uses the three-stage `b58 → b58^10 → b2^64 → b256` pipeline.  No token
 *  prefix or checksum validation is performed.
 *
 *  @param input The Base58-encoded string to decode.
 *  @param out Buffer to receive the big-endian base-256 bytes.
 *  @return On success, a sub-span of `out` covering the result.
 *      On failure, a `std::error_code` (e.g., `BadB58Character`,
 *      `OutputTooSmall`, or `OverflowAdd`).
 *  @note Exposed for unit testing only.
 */
B58Result<std::span<std::uint8_t>>
b58ToB256Be(std::string_view input, std::span<std::uint8_t> out);

}  // namespace detail

}  // namespace b58_fast
#endif  // _MSC_VER
}  // namespace xrpl
