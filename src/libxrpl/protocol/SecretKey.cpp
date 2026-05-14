/** @file
 *  @brief Cryptographic key generation, derivation, and signing for XRPL.
 *
 *  Implements secret-key construction, deterministic key derivation for both
 *  secp256k1 (XRPL's custom BIP-32-predating algorithm via `detail::Generator`)
 *  and Ed25519, public-key derivation, message and digest signing, and
 *  Base58-encoded key parsing.
 *
 *  All intermediate key-material buffers are unconditionally `secureErase`d
 *  on both the success and failure paths to prevent secret bytes from
 *  lingering in freed memory.
 */
#include <xrpl/protocol/SecretKey.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <boost/utility/string_view.hpp>

#include <ed25519.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

namespace xrpl {

SecretKey::~SecretKey()
{
    secureErase(buf_, sizeof(buf_));
}

SecretKey::SecretKey(std::array<std::uint8_t, 32> const& key)
{
    std::memcpy(buf_, key.data(), key.size());
}

SecretKey::SecretKey(Slice const& slice)
{
    if (slice.size() != sizeof(buf_))
        logicError("SecretKey::SecretKey: invalid size");
    std::memcpy(buf_, slice.data(), sizeof(buf_));
}

std::string
SecretKey::toString() const
{
    return strHex(*this);
}

namespace detail {

/** Write a 32-bit unsigned integer into a byte buffer in big-endian order.
 *
 *  Used to append the sequence/counter word when building the hash input
 *  for both `deriveDeterministicRootKey` and `Generator::calculateTweak`.
 *
 *  @param out Pointer to the first of four consecutive bytes to write.
 *  @param v   Value to encode.
 */
void
copyUInt32(std::uint8_t* out, std::uint32_t v)
{
    *out++ = v >> 24;
    *out++ = (v >> 16) & 0xff;
    *out++ = (v >> 8) & 0xff;
    *out = v & 0xff;
}

/** Derive the root secp256k1 secret key from a 128-bit seed.
 *
 *  Hashes `seed || seq` (big-endian 32-bit counter) with SHA-512/Half and
 *  validates the result as a secp256k1 scalar (nonzero and less than the
 *  group order).  The counter loop retries on the negligible chance of an
 *  invalid scalar; after 128 failed attempts a `std::runtime_error` is thrown.
 *
 *  Buffer layout (20 bytes):
 *  @code
 *       0               16   20
 *       |---- seed ------|seq-|
 *  @endcode
 *
 *  The intermediate buffer is always `secureErase`d before returning,
 *  regardless of success or failure.
 *
 *  @param seed  The 128-bit XRPL seed to derive from.
 *  @return The root secret key scalar as a `uint256`.
 *  @throws std::runtime_error if no valid scalar is found within 128 attempts.
 */
uint256
deriveDeterministicRootKey(Seed const& seed)
{
    std::array<std::uint8_t, 20> buf{};
    std::ranges::copy(seed, buf.begin());

    for (std::uint32_t seq = 0; seq != 128; ++seq)
    {
        copyUInt32(buf.data() + 16, seq);

        auto const ret = sha512Half(buf);

        if (secp256k1_ec_seckey_verify(secp256k1Context(), ret.data()) == 1)
        {
            secureErase(buf.data(), buf.size());
            return ret;
        }
    }

    Throw<std::runtime_error>("Unable to derive generator from seed");
}

/** Produces a sequence of secp256k1 key pairs.

    The reference implementation of the XRP Ledger uses a custom derivation
    algorithm which enables the derivation of an entire family of secp256k1
    keypairs from a single 128-bit seed. The algorithm predates widely-used
    standards like BIP-32 and BIP-44.

    Important note to implementers:

        Using this algorithm is not required: all valid secp256k1 keypairs will
        work correctly. Third party implementations can use whatever mechanisms
        they prefer. However, implementers of wallets or other tools that allow
        users to use existing accounts should consider at least supporting this
        derivation technique to make it easier for users to 'import' accounts.

    For more details, please check out:
        https://xrpl.org/cryptographic-keys.html#secp256k1-key-derivation
 */
class Generator
{
private:
    uint256 root_;
    std::array<std::uint8_t, 33> generator_{};

    /** Compute the scalar tweak for the given key-family ordinal.
     *
     *  Hashes `generator || seq || subseq` (both counters big-endian 32-bit)
     *  with SHA-512/Half and validates the result as a secp256k1 scalar.
     *  `subseq` retries on the negligible chance of an invalid scalar.
     *
     *  Buffer layout (41 bytes):
     *  @code
     *       0            33   37   41
     *       |--generator--|seq-|cnt-|
     *  @endcode
     *
     *  The intermediate buffer is always `secureErase`d before returning.
     *
     *  @param seq  Key-family ordinal (0 for the standard single-key case).
     *  @return The tweak scalar as a `uint256`.
     *  @throws std::runtime_error if no valid scalar is found within 128 attempts.
     */
    [[nodiscard]] uint256
    calculateTweak(std::uint32_t seq) const
    {
        std::array<std::uint8_t, 41> buf{};
        std::ranges::copy(generator_, buf.begin());
        copyUInt32(buf.data() + 33, seq);

        for (std::uint32_t subseq = 0; subseq != 128; ++subseq)
        {
            copyUInt32(buf.data() + 37, subseq);

            auto const ret = sha512HalfS(buf);

            if (secp256k1_ec_seckey_verify(secp256k1Context(), ret.data()) == 1)
            {
                secureErase(buf.data(), buf.size());
                return ret;
            }
        }

        Throw<std::runtime_error>("Unable to derive generator from seed");
    }

public:
    /** Construct a Generator from a seed.
     *
     *  Derives the root secret key via `deriveDeterministicRootKey`, then
     *  computes and stores its compressed 33-byte public key as the generator
     *  point used by `calculateTweak` for all subsequent child derivations.
     *
     *  @param seed  The 128-bit XRPL seed.
     *  @throws std::runtime_error propagated from `deriveDeterministicRootKey`
     *      if key derivation fails (statistically negligible).
     */
    explicit Generator(Seed const& seed) : root_(deriveDeterministicRootKey(seed))
    {
        secp256k1_pubkey pubkey;
        if (secp256k1_ec_pubkey_create(secp256k1Context(), &pubkey, root_.data()) != 1)
            logicError("derivePublicKey: secp256k1_ec_pubkey_create failed");

        auto len = generator_.size();

        if (secp256k1_ec_pubkey_serialize(
                secp256k1Context(), generator_.data(), &len, &pubkey, SECP256K1_EC_COMPRESSED) != 1)
            logicError("derivePublicKey: secp256k1_ec_pubkey_serialize failed");
    }

    /** Securely erases all key material on destruction. */
    ~Generator()
    {
        secureErase(root_.data(), root_.size());
        secureErase(generator_.data(), generator_.size());
    }

    /** Generate the nth key pair in this family.
     *
     *  Computes `child = root + tweak(ordinal) mod n` via
     *  `secp256k1_ec_seckey_tweak_add`, then derives the corresponding
     *  compressed public key.  Ordinal 0 is the standard single-key case
     *  used by `generateKeyPair`.
     *
     *  The intermediate copy of the root scalar is `secureErase`d immediately
     *  after the child secret key is constructed.
     *
     *  @param ordinal  Key-family index (0-based).
     *  @return `{PublicKey, SecretKey}` pair for the requested ordinal.
     */
    std::pair<PublicKey, SecretKey>
    operator()(std::size_t ordinal) const
    {
        auto gsk = [this, tweak = calculateTweak(ordinal)]() {
            auto rpk = root_;

            if (secp256k1_ec_seckey_tweak_add(secp256k1Context(), rpk.data(), tweak.data()) == 1)
            {
                SecretKey const sk{Slice{rpk.data(), rpk.size()}};
                secureErase(rpk.data(), rpk.size());
                return sk;
            }

            logicError("Unable to add a tweak!");
        }();

        return {derivePublicKey(KeyType::Secp256k1, gsk), gsk};
    }
};

}  // namespace detail

/** Sign a pre-computed SHA-512/Half digest with a secp256k1 key.
 *
 *  Uses RFC 6979 deterministic nonce generation (`secp256k1_nonce_function_rfc6979`)
 *  to eliminate nonce-reuse risk and make signatures reproducible.  The result
 *  is DER-encoded and at most 72 bytes.
 *
 *  @note This overload is secp256k1-only.  Ed25519 signs the raw message
 *      internally and does not support pre-hashed input.
 *  @param pk      Public key (must be secp256k1; used to determine key type).
 *  @param sk      Corresponding secret key.
 *  @param digest  The 32-byte SHA-512/Half digest to sign.
 *  @return DER-encoded signature in a `Buffer` (up to 72 bytes).
 */
Buffer
signDigest(PublicKey const& pk, SecretKey const& sk, uint256 const& digest)
{
    if (publicKeyType(pk.slice()) != KeyType::Secp256k1)
        logicError("sign: secp256k1 required for digest signing");

    BOOST_ASSERT(sk.size() == 32);
    secp256k1_ecdsa_signature sigImp;
    if (secp256k1_ecdsa_sign(
            secp256k1Context(),
            &sigImp,
            reinterpret_cast<unsigned char const*>(digest.data()),
            reinterpret_cast<unsigned char const*>(sk.data()),
            secp256k1_nonce_function_rfc6979,
            nullptr) != 1)
        logicError("sign: secp256k1_ecdsa_sign failed");

    unsigned char sig[72];
    size_t len = sizeof(sig);
    if (secp256k1_ecdsa_signature_serialize_der(secp256k1Context(), sig, &len, &sigImp) != 1)
        logicError("sign: secp256k1_ecdsa_signature_serialize_der failed");

    return Buffer{sig, len};
}

/** Sign an arbitrary message with a key of the detected type.
 *
 *  Dispatches on the key type embedded in `pk`:
 *  - **Ed25519**: calls `ed25519_sign` directly on the raw message bytes.
 *    Ed25519's security model requires that the library itself hash the
 *    message; bypassing that hash (as `signDigest` does) is not supported.
 *    Returns a fixed 64-byte signature.
 *  - **secp256k1**: applies SHA-512/Half to `m` then signs the resulting
 *    digest with RFC 6979 deterministic nonces.  Returns a DER-encoded
 *    signature of up to 72 bytes.
 *
 *  @param pk       Public key; its type determines the signing algorithm.
 *  @param sk       Corresponding secret key.
 *  @param m        Raw message bytes to sign.
 *  @return Signature in a `Buffer` (64 bytes for Ed25519, ≤72 for secp256k1).
 */
Buffer
sign(PublicKey const& pk, SecretKey const& sk, Slice const& m)
{
    auto const type = publicKeyType(pk.slice());
    if (!type)
        logicError("sign: invalid type");
    switch (*type)
    {
        case KeyType::Ed25519: {
            Buffer b(64);
            ed25519_sign(m.data(), m.size(), sk.data(), pk.data() + 1, b.data());
            return b;
        }
        case KeyType::Secp256k1: {
            sha512_half_hasher h;
            h(m.data(), m.size());
            auto const digest = sha512_half_hasher::result_type(h);

            secp256k1_ecdsa_signature sigImp;
            if (secp256k1_ecdsa_sign(
                    secp256k1Context(),
                    &sigImp,
                    reinterpret_cast<unsigned char const*>(digest.data()),
                    reinterpret_cast<unsigned char const*>(sk.data()),
                    secp256k1_nonce_function_rfc6979,
                    nullptr) != 1)
                logicError("sign: secp256k1_ecdsa_sign failed");

            unsigned char sig[72];
            size_t len = sizeof(sig);
            if (secp256k1_ecdsa_signature_serialize_der(secp256k1Context(), sig, &len, &sigImp) !=
                1)
                logicError("sign: secp256k1_ecdsa_signature_serialize_der failed");

            return Buffer{sig, len};
        }
        default:
            logicError("sign: invalid type");
    }
}

/** Generate a secret key from the platform CSPRNG.
 *
 *  Fills a 32-byte stack buffer via `beast::rngfill` / `cryptoPrng()`, wraps
 *  it in a `SecretKey`, then immediately `secureErase`s the stack buffer.
 *  The result is not tied to any seed and cannot be deterministically
 *  reproduced — use `generateKeyPair` when wallet recovery is required.
 *
 *  @return A freshly generated `SecretKey` backed by cryptographically
 *      secure random bytes.
 */
SecretKey
randomSecretKey()
{
    std::uint8_t buf[32];
    beast::rngfill(buf, sizeof(buf), cryptoPrng());
    SecretKey const sk(Slice{buf, sizeof(buf)});
    secureErase(buf, sizeof(buf));
    return sk;
}

/** Derive a secret key deterministically from a seed.
 *
 *  - **Ed25519**: computes `sha512HalfS(seed)` directly — no counter loop is
 *    needed because Ed25519's scalar space is large enough to accept any
 *    256-bit hash output.
 *  - **secp256k1**: delegates to `detail::deriveDeterministicRootKey`, which
 *    hashes `seed || counter` and validates against the curve order.
 *
 *  In both cases the intermediate key buffer is `secureErase`d before return.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param seed  The 128-bit XRPL seed.
 *  @return The derived `SecretKey`.
 *  @throws std::runtime_error (secp256k1 only) if derivation fails after 128
 *      attempts, propagated from `deriveDeterministicRootKey`.
 */
SecretKey
generateSecretKey(KeyType type, Seed const& seed)
{
    if (type == KeyType::Ed25519)
    {
        auto key = sha512HalfS(Slice(seed.data(), seed.size()));
        SecretKey const sk{Slice{key.data(), key.size()}};
        secureErase(key.data(), key.size());
        return sk;
    }

    if (type == KeyType::Secp256k1)
    {
        auto key = detail::deriveDeterministicRootKey(seed);
        SecretKey const sk{Slice{key.data(), key.size()}};
        secureErase(key.data(), key.size());
        return sk;
    }

    logicError("generateSecretKey: unknown key type");
}

/** Derive the public key from a secret key.
 *
 *  - **secp256k1**: produces a compressed 33-byte point via
 *    `secp256k1_ec_pubkey_create` + `secp256k1_ec_pubkey_serialize`.
 *  - **Ed25519**: produces a 33-byte key where `buf[0] == 0xED` and
 *    `buf[1..32]` is the 32-byte Ed25519 public key.  The `0xED` prefix is
 *    the XRPL convention for distinguishing Ed25519 keys on the wire from
 *    secp256k1 keys (which start with `0x02` or `0x03`).
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param sk    The secret key to derive from.
 *  @return The corresponding `PublicKey`.
 */
PublicKey
derivePublicKey(KeyType type, SecretKey const& sk)
{
    switch (type)
    {
        case KeyType::Secp256k1: {
            secp256k1_pubkey pubkeyImp;
            if (secp256k1_ec_pubkey_create(
                    secp256k1Context(),
                    &pubkeyImp,
                    reinterpret_cast<unsigned char const*>(sk.data())) != 1)
                logicError("derivePublicKey: secp256k1_ec_pubkey_create failed");

            unsigned char pubkey[33];
            std::size_t len = sizeof(pubkey);
            if (secp256k1_ec_pubkey_serialize(
                    secp256k1Context(), pubkey, &len, &pubkeyImp, SECP256K1_EC_COMPRESSED) != 1)
                logicError("derivePublicKey: secp256k1_ec_pubkey_serialize failed");

            return PublicKey{Slice{pubkey, len}};
        }
        case KeyType::Ed25519: {
            unsigned char buf[33];
            buf[0] = 0xED;
            ed25519_publickey(sk.data(), &buf[1]);
            return PublicKey(Slice{buf, sizeof(buf)});
        }
        default:
            logicError("derivePublicKey: bad key type");
    };
}

/** Generate a key pair deterministically from a seed.
 *
 *  - **secp256k1**: constructs a `detail::Generator` from the seed and
 *    returns the key pair at ordinal 0 (the standard single-key derivation).
 *    The generator-based design supports key families but ordinal 0 is used
 *    in the vast majority of cases.
 *  - **Ed25519**: calls `generateSecretKey` then `derivePublicKey` directly;
 *    no generator indirection is needed.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @param seed  The 128-bit XRPL seed.
 *  @return `{PublicKey, SecretKey}` pair.
 *  @throws std::runtime_error propagated if secp256k1 root-key derivation
 *      fails (statistically negligible).
 */
std::pair<PublicKey, SecretKey>
generateKeyPair(KeyType type, Seed const& seed)
{
    switch (type)
    {
        case KeyType::Secp256k1: {
            detail::Generator const g(seed);
            return g(0);
        }
        default:
        case KeyType::Ed25519: {
            auto const sk = generateSecretKey(type, seed);
            return {derivePublicKey(type, sk), sk};
        }
    }
}

/** Generate a key pair from the CSPRNG (non-deterministic).
 *
 *  Combines `randomSecretKey` with `derivePublicKey`.  Unlike
 *  `generateKeyPair`, the result cannot be reproduced from any seed, making
 *  it suitable for one-off key generation where wallet recovery is not needed.
 *
 *  @param type  Algorithm (`KeyType::Ed25519` or `KeyType::Secp256k1`).
 *  @return `{PublicKey, SecretKey}` pair backed by random key material.
 */
std::pair<PublicKey, SecretKey>
randomKeyPair(KeyType type)
{
    auto const sk = randomSecretKey();
    return {derivePublicKey(type, sk), sk};
}

/** Decode a Base58Check-encoded secret key.
 *
 *  Decodes the token via `decodeBase58Token` and validates that the payload
 *  is exactly 32 bytes.  Returns `std::nullopt` on decoding failure or if
 *  the payload length is wrong.
 *
 *  @tparam SecretKey  Explicit template specialization — not a generic helper.
 *  @param type  The expected `TokenType` prefix (e.g. `TokenType::FamilySeed`).
 *  @param s     Base58Check-encoded string to decode.
 *  @return The decoded `SecretKey`, or `std::nullopt` on any error.
 */
template <>
std::optional<SecretKey>
parseBase58(TokenType type, std::string const& s)
{
    auto const result = decodeBase58Token(s, type);
    if (result.empty())
        return std::nullopt;
    if (result.size() != 32)
        return std::nullopt;
    return SecretKey(makeSlice(result));
}

}  // namespace xrpl
