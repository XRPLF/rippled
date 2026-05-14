#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/tokens.h>

#include <array>
#include <optional>

namespace xrpl {

/** A 128-bit secret seed from which all XRPL key material is derived.
 *
 *  A `Seed` is the root secret in the XRPL key hierarchy. From a single seed,
 *  a deterministic derivation produces the private key, public key, and account
 *  address. The class enforces two security invariants:
 *
 *  - **No default construction.** A zero-initialized seed could be mistaken
 *    for valid entropy, so every `Seed` must be explicitly constructed from
 *    real material.
 *  - **Secure destruction.** The destructor calls `secure_erase()` on the
 *    internal buffer to overwrite key material in heap/stack memory before
 *    the object is released. CPU caches and registers may still retain
 *    remnants; this is a best-effort measure consistent with industry practice.
 *
 *  Copy construction and assignment are allowed so seeds can be passed by
 *  value into key-derivation functions. Callers should minimize the number
 *  of live copies.
 *
 *  Only `const` iterators and `data()` are exposed, preventing external
 *  mutation of the raw key material.
 *
 *  @see randomSeed(), generateSeed(), parseGenericSeed(), parseBase58<Seed>()
 *  @see SecretKey.h for the derivation step that consumes a Seed
 */
class Seed
{
private:
    std::array<uint8_t, 16> buf_{};

public:
    using const_iterator = std::array<uint8_t, 16>::const_iterator;

    Seed() = delete;

    Seed(Seed const&) = default;
    Seed&
    operator=(Seed const&) = default;

    /** Destroy the seed, securely erasing the internal buffer first. */
    ~Seed();

    /** Construct a seed from a byte slice.
     *
     *  @param slice Raw bytes to copy into the seed buffer.
     *  @throws LogicError if `slice.size() != 16`.
     */
    explicit Seed(Slice const& slice);

    /** Construct a seed from a 128-bit integer.
     *
     *  @param seed The 128-bit value whose raw bytes are copied into the seed
     *      buffer.
     *  @throws LogicError if `seed.size() != 16`.
     */
    explicit Seed(uint128 const& seed);

    /** Return a pointer to the first byte of the seed buffer. */
    [[nodiscard]] std::uint8_t const*
    data() const
    {
        return buf_.data();
    }

    /** Return the size of the seed buffer in bytes (always 16). */
    [[nodiscard]] std::size_t
    size() const
    {
        return buf_.size();
    }

    /** Return a const iterator to the first byte of the seed buffer. */
    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return buf_.begin();
    }

    /** Return a const iterator to the first byte of the seed buffer. */
    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return buf_.cbegin();
    }

    /** Return a const iterator past the last byte of the seed buffer. */
    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return buf_.end();
    }

    /** Return a const iterator past the last byte of the seed buffer. */
    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return buf_.cend();
    }
};

//------------------------------------------------------------------------------

/** Generate a cryptographically secure random seed.
 *
 *  Fills a temporary staging buffer via `beast::rngfill()` backed by the
 *  global CSPRNG (`crypto_prng()`), constructs the `Seed` from it, and then
 *  immediately calls `secure_erase()` on the staging buffer before returning.
 *  The staging buffer is erased explicitly because its stack lifetime would
 *  otherwise extend past the point where the seed has been captured.
 *
 *  @return A freshly generated, cryptographically random seed.
 */
Seed
randomSeed();

/** Derive a seed deterministically from a passphrase.
 *
 *  Implements the XRPL passphrase-to-seed algorithm: the seed is the first
 *  128 bits of SHA-512-Half applied to the raw passphrase bytes (no null
 *  terminator included). The hasher type used (`sha512_half_hasher_s`)
 *  securely erases its internal state on destruction.
 *
 *  @param passPhrase Arbitrary string treated as raw bytes; not interpreted
 *      as hex or Base58.
 *  @return The deterministic seed for the given passphrase.
 *  @note To parse a string that might be hex, Base58, RFC1751, or a
 *      passphrase, use `parseGenericSeed()` instead.
 */
Seed
generateSeed(std::string const& passPhrase);

/** Decode a Base58Check-encoded seed string.
 *
 *  Decodes a string carrying the `TokenType::FamilySeed` prefix (the
 *  well-known "s"-prefixed wallet seed strings). The decoded payload must
 *  be exactly 16 bytes; any other length yields `std::nullopt`.
 *
 *  @param s A Base58Check-encoded string.
 *  @return The decoded seed, or `std::nullopt` if the string is empty,
 *      malformed, has an incorrect checksum, or decodes to a payload of
 *      the wrong size.
 */
template <>
std::optional<Seed>
parseBase58(std::string const& s);

/** Parse a string in any recognized seed format.
 *
 *  Attempts each format in order, returning on the first match:
 *
 *  1. **Rejection guard.** Returns `std::nullopt` if the string successfully
 *     parses as an `AccountID`, node public key, account public key, node
 *     private key, or account secret. This prevents accidentally using an
 *     address or public key as a seed.
 *  2. **Empty string.** Returns `std::nullopt`.
 *  3. **Hex.** A 32-character hex string is decoded directly as a 128-bit
 *     seed.
 *  4. **Base58 family seed.** Delegates to `parseBase58<Seed>()`.
 *  5. **RFC1751 mnemonic** (only when `rfc1751 = true`). A 12-word
 *     English mnemonic decoded per RFC1751 with XRPL's historical
 *     byte-reversal convention. Parity errors cause fallthrough to the
 *     passphrase step rather than returning `std::nullopt`.
 *  6. **Passphrase fallback.** Any non-empty string that does not match
 *     the above is passed to `generateSeed()`. This step always succeeds,
 *     so a non-empty string that is not a recognized key type will always
 *     produce a seed.
 *
 *  @param str The string to parse.
 *  @param rfc1751 When `false`, RFC1751 mnemonic decoding is skipped.
 *      Pass `false` in contexts where strict format enforcement is required
 *      (e.g., node identity from the command line).
 *  @return The parsed seed, or `std::nullopt` if the string is empty or
 *      was recognized as a non-seed key type.
 *  @note The passphrase fallback means this function never returns
 *      `std::nullopt` for a non-empty string unless it matches a
 *      disallowed key type.
 */
std::optional<Seed>
parseGenericSeed(std::string const& str, bool rfc1751 = true);

/** Encode a seed as an RFC1751 English mnemonic.
 *
 *  Produces a 12-word phrase using the RFC1751 dictionary. XRPL reverses
 *  the byte order of the seed before encoding — `parseGenericSeed()`
 *  applies the same reversal symmetrically when decoding.
 *
 *  @param seed The seed to encode.
 *  @return A space-separated 12-word RFC1751 mnemonic string.
 *  @note RFC1751 output is considered deprecated. `parseGenericSeed()`
 *      accepts it by default for backward compatibility; pass
 *      `rfc1751 = false` to disable that fallback.
 */
std::string
seedAs1751(Seed const& seed);

/** Encode a seed as a Base58Check string with the `FamilySeed` token type.
 *
 *  Produces the well-known "s"-prefixed wallet seed strings displayed to
 *  XRPL users.
 *
 *  @param seed The seed to encode.
 *  @return The Base58Check-encoded seed string.
 */
inline std::string
toBase58(Seed const& seed)
{
    return encodeBase58Token(TokenType::FamilySeed, seed.data(), seed.size());
}

}  // namespace xrpl
