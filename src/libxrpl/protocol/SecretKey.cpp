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

void
copyUint32(std::uint8_t* out, std::uint32_t v)
{
    *out++ = v >> 24;
    *out++ = (v >> 16) & 0xff;
    *out++ = (v >> 8) & 0xff;
    *out = v & 0xff;
}

uint256
deriveDeterministicRootKey(Seed const& seed)
{
    // We fill this buffer with the seed and append a 32-bit "counter"
    // that counts how many attempts we've had to make to generate a
    // non-zero key that's less than the curve's order:
    //
    //                       1    2
    //      0                6    0
    // buf  |----------------|----|
    //      |      seed      | seq|

    std::array<std::uint8_t, 20> buf{};
    std::ranges::copy(seed, buf.begin());

    // The odds that this loop executes more than once are negligible
    // but *just* in case someone managed to generate a key that required
    // more iterations loop a few times.
    for (std::uint32_t seq = 0; seq != 128; ++seq)
    {
        copyUint32(buf.data() + 16, seq);

        auto const ret = sha512Half(buf);

        if (secp256k1_ec_seckey_verify(secp256k1Context(), ret.data()) == 1)
        {
            secureErase(buf.data(), buf.size());
            return ret;
        }
    }

    Throw<std::runtime_error>("Unable to derive generator from seed");
}

//------------------------------------------------------------------------------
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

    [[nodiscard]] uint256
    calculateTweak(std::uint32_t seq) const
    {
        // We fill the buffer with the generator, the provided sequence
        // and a 32-bit counter tracking the number of attempts we have
        // already made looking for a non-zero key that's less than the
        // curve's order:
        //                                        3    3    4
        //      0          pubGen                 3    7    1
        // buf  |---------------------------------|----|----|
        //      |            generator            | seq| cnt|

        std::array<std::uint8_t, 41> buf{};
        std::ranges::copy(generator_, buf.begin());
        copyUint32(buf.data() + 33, seq);

        // The odds that this loop executes more than once are negligible
        // but we impose a maximum limit just in case.
        for (std::uint32_t subseq = 0; subseq != 128; ++subseq)
        {
            copyUint32(buf.data() + 37, subseq);

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

    ~Generator()
    {
        secureErase(root_.data(), root_.size());
        secureErase(generator_.data(), generator_.size());
    }

    /** Generate the nth key pair. */
    std::pair<PublicKey, SecretKey>
    operator()(std::size_t ordinal) const
    {
        // Generates Nth secret key:
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

SecretKey
randomSecretKey()
{
    std::uint8_t buf[32];
    beast::rngfill(buf, sizeof(buf), cryptoPrng());
    SecretKey const sk(Slice{buf, sizeof(buf)});
    secureErase(buf, sizeof(buf));
    return sk;
}

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

std::pair<PublicKey, SecretKey>
randomKeyPair(KeyType type)
{
    auto const sk = randomSecretKey();
    return {derivePublicKey(type, sk), sk};
}

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
