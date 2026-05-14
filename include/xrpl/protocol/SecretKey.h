#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>

#include <array>
#include <cstring>
#include <string>

namespace xrpl {

/** A 32-byte private key for either secp256k1 or Ed25519.
 *
 *  The destructor unconditionally zeroes the backing buffer via `secureErase`,
 *  defending against cold-boot and memory-dump attacks. Intermediate buffers
 *  in all key-generation and derivation helpers are likewise erased.
 *
 *  Comparison operators are deleted: comparing secret keys in application code
 *  is almost always a mistake (compare public keys or `AccountID`s instead),
 *  and any comparison implementation risks timing-observable branches that
 *  could leak key material through a side channel.
 *
 *  `operator<<` is absent by design — streaming a secret key to a log or debug
 *  output is too easy an accident. Use `toString()` for the rare legitimate case.
 *
 *  @note The default constructor is deleted; a `SecretKey` must always be
 *      initialised with actual key material.
 */
class SecretKey
{
public:
    /** Size of the raw key buffer in bytes. */
    static constexpr std::size_t kSIZE = 32;

private:
    std::uint8_t buf_[kSIZE]{};

public:
    using const_iterator = std::uint8_t const*;

    SecretKey() = delete;
    SecretKey(SecretKey const&) = default;
    SecretKey&
    operator=(SecretKey const&) = default;

    /** Deleted: comparing secret keys risks timing side-channel leaks. */
    bool
    operator==(SecretKey const&) = delete;

    /** Deleted: comparing secret keys risks timing side-channel leaks. */
    bool
    operator!=(SecretKey const&) = delete;

    /** Zeroes the key buffer via `secureErase` before releasing memory. */
    ~SecretKey();

    /** Construct from a 32-byte array.
     *
     *  @param data Raw key material; copied into the internal buffer.
     */
    SecretKey(std::array<std::uint8_t, kSIZE> const& data);

    /** Construct from a `Slice`.
     *
     *  @param slice Raw key material; must be exactly 32 bytes.
     *  @throws LogicError if `slice.size() != 32`.
     */
    SecretKey(Slice const& slice);

    /** @return Pointer to the first byte of the raw 32-byte key material. */
    [[nodiscard]] std::uint8_t const*
    data() const
    {
        return buf_;
    }

    /** @return Number of bytes in the key buffer (always 32). */
    [[nodiscard]] std::size_t
    size() const
    {
        return sizeof(buf_);
    }

    /** Return the key as a hexadecimal string.
     *
     *  Use this only where the hex representation is genuinely required
     *  (e.g. CLI tooling). Prefer keeping the key in its binary form
     *  everywhere else. `operator<<` is intentionally absent to prevent
     *  accidental exposure in log output.
     *
     *  @return Hex-encoded string of the 32-byte key.
     */
    [[nodiscard]] std::string
    toString() const;

    /** @return Iterator to the first byte of the key buffer. */
    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return buf_;
    }

    /** @return Iterator to the first byte of the key buffer. */
    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return buf_;
    }

    /** @return Past-the-end iterator for the key buffer. */
    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return buf_ + sizeof(buf_);
    }

    /** @return Past-the-end iterator for the key buffer. */
    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return buf_ + sizeof(buf_);
    }
};

/** Deleted: comparing secret keys risks timing side-channel leaks. */
bool
operator==(SecretKey const& lhs, SecretKey const& rhs) = delete;

/** Deleted: comparing secret keys risks timing side-channel leaks. */
bool
operator!=(SecretKey const& lhs, SecretKey const& rhs) = delete;

//------------------------------------------------------------------------------

/** Decode a Base58Check-encoded secret key.
 *
 *  Decodes the token and validates that the payload is exactly 32 bytes.
 *  Never throws — returns `std::nullopt` on decoding failure or length
 *  mismatch.
 *
 *  @param type The expected `TokenType` prefix (e.g. `TokenType::FamilySeed`).
 *  @param s    Base58Check-encoded string to decode.
 *  @return The decoded `SecretKey`, or `std::nullopt` on any error.
 */
template <>
std::optional<SecretKey>
parseBase58(TokenType type, std::string const& s);

/** Encode a secret key as a Base58Check string.
 *
 *  The `TokenType` argument controls the version byte prepended during
 *  encoding, consistent with the XRPL token system (e.g. `TokenType::FamilySeed`).
 *
 *  @param type Version byte selector for the Base58Check envelope.
 *  @param sk   The secret key to encode.
 *  @return Base58Check-encoded string.
 */
inline std::string
toBase58(TokenType type, SecretKey const& sk)
{
    return encodeBase58Token(type, sk.data(), sk.size());
}

/** Generate a secret key from the platform CSPRNG.
 *
 *  Fills 32 bytes from `crypto_prng()`, constructs the key, then immediately
 *  erases the temporary stack buffer. The result is not tied to any seed and
 *  cannot be deterministically reproduced — use `generateKeyPair` when wallet
 *  recovery is required.
 *
 *  @return A freshly generated `SecretKey` backed by cryptographically secure
 *      random bytes.
 */
SecretKey
randomSecretKey();

/** Derive a secret key deterministically from a seed.
 *
 *  - **Ed25519**: the secret key is `sha512Half(seed)` directly.
 *  - **secp256k1**: hashes `seed || counter` with SHA512-Half, retrying with
 *    an incrementing counter until the result is a valid curve scalar. In
 *    practice this loop almost never executes more than once.
 *
 *  All intermediate key-material buffers are erased before return.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param seed  The 128-bit XRPL seed.
 *  @return The derived `SecretKey`.
 *  @throws std::runtime_error (secp256k1 only) if no valid scalar is found
 *      within 128 attempts (statistically negligible).
 */
SecretKey
generateSecretKey(KeyType type, Seed const& seed);

/** Derive the public key corresponding to a secret key.
 *
 *  - **secp256k1**: produces a 33-byte compressed curve point.
 *  - **Ed25519**: produces a 33-byte key where `buf[0] == 0xED` followed by
 *    the 32-byte Edwards-curve public key. The `0xED` prefix is the XRPL
 *    wire convention that `publicKeyType()` uses to distinguish Ed25519 keys
 *    from secp256k1 keys (which start with `0x02` or `0x03`).
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param sk    The secret key to derive from.
 *  @return The corresponding `PublicKey`.
 */
PublicKey
derivePublicKey(KeyType type, SecretKey const& sk);

/** Generate a key pair deterministically from a seed.
 *
 *  This is the main entry point for wallet-style key derivation.
 *
 *  - **secp256k1**: uses XRPL's custom two-level derivation algorithm (which
 *    predates BIP-32). A root private key is derived from the seed, its
 *    compressed public key becomes the "generator point", and the child key
 *    at ordinal 0 is produced by tweaking the root with a SHA512-Half of
 *    the generator concatenated with the ordinal. Third-party wallets that
 *    need to import existing XRPL accounts should support this algorithm.
 *  - **Ed25519**: equivalent to calling `generateSecretKey` then
 *    `derivePublicKey` directly; no generator indirection is used.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param seed  The 128-bit XRPL seed.
 *  @return `{PublicKey, SecretKey}` pair.
 *  @throws std::runtime_error propagated from secp256k1 root-key derivation
 *      if no valid scalar is found within 128 attempts (statistically
 *      negligible).
 *  @see https://xrpl.org/cryptographic-keys.html#secp256k1-key-derivation
 */
std::pair<PublicKey, SecretKey>
generateKeyPair(KeyType type, Seed const& seed);

/** Generate a key pair from the platform CSPRNG (non-deterministic).
 *
 *  Combines `randomSecretKey()` with `derivePublicKey()`. Unlike
 *  `generateKeyPair()`, the result cannot be reproduced from any seed.
 *  Use this when wallet recovery is not needed.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @return `{PublicKey, SecretKey}` pair backed by random key material.
 */
std::pair<PublicKey, SecretKey>
randomKeyPair(KeyType type);

/** Sign a pre-computed digest with a secp256k1 key.
 *
 *  Restricted to secp256k1: Ed25519's security proof depends on how the
 *  message is hashed internally by the primitive, so pre-hashed signing is
 *  not supported for Ed25519. Passes a `LogicError` if `pk` is not a
 *  secp256k1 key.
 *
 *  The ECDSA nonce is generated deterministically per RFC 6979, eliminating
 *  the class of vulnerabilities caused by weak random nonces. The result is
 *  DER-encoded and at most 72 bytes.
 *
 *  @param pk      Public key; must be secp256k1 (used to verify the key type).
 *  @param sk      Corresponding secret key.
 *  @param digest  The 32-byte SHA512-Half digest to sign.
 *  @return DER-encoded signature in a `Buffer` (up to 72 bytes).
 */
/** @{ */
Buffer
signDigest(PublicKey const& pk, SecretKey const& sk, uint256 const& digest);

/** Sign a pre-computed digest, deriving the public key from `type` and `sk`.
 *
 *  Convenience overload that calls `derivePublicKey(type, sk)` internally.
 *  Restricted to secp256k1 — see the primary overload for details.
 *
 *  @param type    Must be `KeyType::Secp256k1`.
 *  @param sk      The secret key to sign with.
 *  @param digest  The 32-byte SHA512-Half digest to sign.
 *  @return DER-encoded signature in a `Buffer` (up to 72 bytes).
 */
inline Buffer
signDigest(KeyType type, SecretKey const& sk, uint256 const& digest)
{
    return signDigest(derivePublicKey(type, sk), sk, digest);
}
/** @} */

/** Sign a raw message with a key of the detected type.
 *
 *  Dispatches on the key type embedded in `pk`:
 *  - **Ed25519**: passes the raw message bytes directly to `ed25519_sign`,
 *    which incorporates its own deterministic internal hashing. Returns a
 *    fixed 64-byte signature.
 *  - **secp256k1**: applies SHA512-Half to the message then signs the
 *    resulting digest with RFC 6979 deterministic nonces. Returns a
 *    DER-encoded signature of up to 72 bytes.
 *
 *  @param pk       Public key; its type determines the signing algorithm.
 *  @param sk       Corresponding secret key.
 *  @param message  Raw message bytes to sign.
 *  @return Signature in a `Buffer` (64 bytes for Ed25519, ≤72 for secp256k1).
 */
/** @{ */
Buffer
sign(PublicKey const& pk, SecretKey const& sk, Slice const& message);

/** Sign a raw message, deriving the public key from `type` and `sk`.
 *
 *  Convenience overload that calls `derivePublicKey(type, sk)` internally.
 *  See the primary overload for signing semantics.
 *
 *  @param type     Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param sk       The secret key to sign with.
 *  @param message  Raw message bytes to sign.
 *  @return Signature in a `Buffer` (64 bytes for Ed25519, ≤72 for secp256k1).
 */
inline Buffer
sign(KeyType type, SecretKey const& sk, Slice const& message)
{
    return sign(derivePublicKey(type, sk), sk, message);
}
/** @} */

}  // namespace xrpl
