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
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <boost/utility/string_view.hpp>

#include <ed25519.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>


#pragma push_macro("L")
#pragma push_macro("K")
#pragma push_macro("N")
#pragma push_macro("S")
#pragma push_macro("U")
#pragma push_macro("D")
#undef L
#undef K
#undef N
#undef S
#undef U
#undef D

extern "C" {
#include "api.h"
#include "fips202.h"
#include "packing.h"
#include "params.h"
#include "poly.h"
#include "polyvec.h"
#include "sign.h"
}

#include <iomanip>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>

// Define the dilithium functions and sizes with respect to functions named here
#ifndef CRYPTO_PUBLICKEYBYTES
#define CRYPTO_PUBLICKEYBYTES pqcrystals_dilithium2_PUBLICKEYBYTES
#endif

#ifndef CRYPTO_SECRETKEYBYTES
#define CRYPTO_SECRETKEYBYTES pqcrystals_dilithium2_SECRETKEYBYTES
#endif

#ifndef CRYPTO_BYTES
#define CRYPTO_BYTES pqcrystals_dilithium2_BYTES
#endif

#ifndef crypto_sign_keypair
#define crypto_sign_keypair pqcrystals_dilithium2_ref_keypair
#endif

#ifndef crypto_sign_signature
#define crypto_sign_signature pqcrystals_dilithium2_ref_signature
#endif

extern "C" void randombytes(uint8_t* buf, size_t size)
{
    beast::rngfill(buf, size, xrpl::crypto_prng());
}

namespace xrpl {

SecretKey::~SecretKey()
{
    secure_erase(buf_, size_);
}

SecretKey::SecretKey(std::array<std::uint8_t, 32> const& key)
{
    size_ = 32;
    std::memcpy(buf_, key.data(), key.size());
}

SecretKey::SecretKey(std::array<std::uint8_t, 2560> const& key)
{
    size_ = 2560;
    std::memcpy(buf_, key.data(), key.size());
}

SecretKey::SecretKey(Slice const& slice)
{
    if (slice.size() != 32 && slice.size() != 2560)
    {
        LogicError("SecretKey::SecretKey: invalid size");
    }
    size_ = slice.size();
    std::memcpy(buf_, slice.data(), size_);
}

std::string
SecretKey::to_string() const
{
    return strHex(*this);
}

namespace detail {

void
copy_uint32(std::uint8_t* out, std::uint32_t v)
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

    std::array<std::uint8_t, 20> buf;
    std::copy(seed.begin(), seed.end(), buf.begin());

    // The odds that this loop executes more than once are neglible
    // but *just* in case someone managed to generate a key that required
    // more iterations loop a few times.
    for (std::uint32_t seq = 0; seq != 128; ++seq)
    {
        copy_uint32(buf.data() + 16, seq);

        auto const ret = sha512Half(buf);

        if (secp256k1_ec_seckey_verify(secp256k1Context(), ret.data()) == 1)
        {
            secure_erase(buf.data(), buf.size());
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
    std::array<std::uint8_t, 33> generator_;

    uint256
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

        std::array<std::uint8_t, 41> buf;
        std::copy(generator_.begin(), generator_.end(), buf.begin());
        copy_uint32(buf.data() + 33, seq);

        // The odds that this loop executes more than once are neglible
        // but we impose a maximum limit just in case.
        for (std::uint32_t subseq = 0; subseq != 128; ++subseq)
        {
            copy_uint32(buf.data() + 37, subseq);

            auto const ret = sha512Half_s(buf);

            if (secp256k1_ec_seckey_verify(secp256k1Context(), ret.data()) == 1)
            {
                secure_erase(buf.data(), buf.size());
                return ret;
            }
        }

        Throw<std::runtime_error>("Unable to derive generator from seed");
    }

public:
    explicit Generator(Seed const& seed)
        : root_(deriveDeterministicRootKey(seed))
    {
        secp256k1_pubkey pubkey;
        if (secp256k1_ec_pubkey_create(
                secp256k1Context(), &pubkey, root_.data()) != 1)
            LogicError("derivePublicKey: secp256k1_ec_pubkey_create failed");

        auto len = generator_.size();

        if (secp256k1_ec_pubkey_serialize(
                secp256k1Context(),
                generator_.data(),
                &len,
                &pubkey,
                SECP256K1_EC_COMPRESSED) != 1)
            LogicError("derivePublicKey: secp256k1_ec_pubkey_serialize failed");
    }

    ~Generator()
    {
        secure_erase(root_.data(), root_.size());
        secure_erase(generator_.data(), generator_.size());
    }

    /** Generate the nth key pair. */
    std::pair<PublicKey, SecretKey>
    operator()(std::size_t ordinal) const
    {
        // Generates Nth secret key:
        auto gsk = [this, tweak = calculateTweak(ordinal)]() {
            auto rpk = root_;

            if (secp256k1_ec_seckey_tweak_add(
                    secp256k1Context(), rpk.data(), tweak.data()) == 1)
            {
                SecretKey sk{Slice{rpk.data(), rpk.size()}};
                secure_erase(rpk.data(), rpk.size());
                return sk;
            }

            LogicError("Unable to add a tweak!");
        }();

        return {derivePublicKey(KeyType::secp256k1, gsk), gsk};
    }
};

}  // namespace detail

Buffer
signDigest(PublicKey const& pk, SecretKey const& sk, uint256 const& digest)
{
    auto const type = publicKeyType(pk.slice());
    if (!type)
        LogicError("signDigest: invalid key type");

    switch (*type)
    {
        case KeyType::secp256k1: {
            BOOST_ASSERT(sk.size() == 32);
            secp256k1_ecdsa_signature sig_imp;
            if (secp256k1_ecdsa_sign(
                    secp256k1Context(),
                    &sig_imp,
                    reinterpret_cast<unsigned char const*>(digest.data()),
                    reinterpret_cast<unsigned char const*>(sk.data()),
                    secp256k1_nonce_function_rfc6979,
                    nullptr) != 1)
                LogicError("sign: secp256k1_ecdsa_sign failed");

            unsigned char sig[72];
            size_t len = sizeof(sig);
            if (secp256k1_ecdsa_signature_serialize_der(
                    secp256k1Context(), sig, &len, &sig_imp) != 1)
                LogicError("sign: secp256k1_ecdsa_signature_serialize_der failed");

            return Buffer{sig, len};
        }
        case KeyType::dilithium: {
            uint8_t sig[CRYPTO_BYTES];
            size_t len = 0;
            uint8_t ctx[] = {};
            size_t ctxlen = 0;
            // Sign the digest data directly
            crypto_sign_signature(
                sig,
                &len,
                reinterpret_cast<unsigned char const*>(digest.data()),
                digest.size(),
                ctx,
                ctxlen,
                sk.data());
            return Buffer{sig, len};
        }
        default:
            LogicError("signDigest: unsupported key type");
    }
}

std::string
toHexString(const uint8_t* data, size_t length)
{
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i)
    {
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

Buffer
sign(PublicKey const& pk, SecretKey const& sk, Slice const& m)
{
    auto const type = publicKeyType(pk.slice());
    if (!type)
        LogicError("sign: invalid type");
    switch (*type)
    {
        case KeyType::ed25519: {
            Buffer b(64);
            ed25519_sign(
                m.data(), m.size(), sk.data(), pk.data() + 1, b.data());
            return b;
        }
        case KeyType::secp256k1: {
            sha512_half_hasher h;
            h(m.data(), m.size());
            auto const digest = sha512_half_hasher::result_type(h);

            secp256k1_ecdsa_signature sig_imp;
            if (secp256k1_ecdsa_sign(
                    secp256k1Context(),
                    &sig_imp,
                    reinterpret_cast<unsigned char const*>(digest.data()),
                    reinterpret_cast<unsigned char const*>(sk.data()),
                    secp256k1_nonce_function_rfc6979,
                    nullptr) != 1)
                LogicError("sign: secp256k1_ecdsa_sign failed");

            unsigned char sig[72];
            size_t len = sizeof(sig);
            if (secp256k1_ecdsa_signature_serialize_der(
                    secp256k1Context(), sig, &len, &sig_imp) != 1)
                LogicError(
                    "sign: secp256k1_ecdsa_signature_serialize_der failed");
            
            return Buffer{sig, len};
        }
        case KeyType::dilithium: {
            uint8_t sig[CRYPTO_BYTES];
            size_t len = 0;
            uint8_t ctx[] = {}; // or your context string
            size_t ctxlen = 0;  // or your context length
            crypto_sign_signature(sig, &len, m.data(), m.size(), ctx, ctxlen, sk.data());
            return Buffer{sig, len};
        }
        default:
            LogicError("sign: invalid type");
    }
}

SecretKey
randomSecretKey()
{
    return randomSecretKey(KeyType::secp256k1);
}

SecretKey
randomSecretKey(KeyType type)
{
    switch (type)
    {
        case KeyType::ed25519:
        case KeyType::secp256k1: {
            std::uint8_t buf[32];
            beast::rngfill(buf, sizeof(buf), crypto_prng());
            SecretKey sk(Slice{buf, sizeof(buf)});
            secure_erase(buf, sizeof(buf));
            return sk;
        }
        case KeyType::dilithium: {
            uint8_t pk[CRYPTO_PUBLICKEYBYTES];
            uint8_t buf[CRYPTO_SECRETKEYBYTES];
            crypto_sign_keypair(pk, buf);
            SecretKey sk(Slice{buf, CRYPTO_SECRETKEYBYTES});
            secure_erase(buf, sizeof(buf));
            return sk;
        }
        default:
            LogicError("randomSecretKey: invalid KeyType");
    }
}

void
expand_mat(polyvecl mat[K], const uint8_t rho[SEEDBYTES])
{
    unsigned int i, j;
    uint16_t nonce;

    for (i = 0; i < K; ++i)
    {
        for (j = 0; j < L; ++j)
        {
            nonce = (i << 8) + j;  // Combine indices i and j into a nonce
            poly_uniform(&mat[i].vec[j], rho, nonce);
        }
    }
}

int
pqcrystals_dilithium2_ref_keypair_seed(
    uint8_t* pk,
    uint8_t* sk,
    const uint8_t* seed)
{
    uint8_t seedbuf[3 * SEEDBYTES];
    uint8_t tr[CRHBYTES];
    const uint8_t* rho;
    const uint8_t* rhoprime;
    const uint8_t* key;
    polyvecl mat[K], s1, s1hat;
    polyveck t1, t0, s2;
    unsigned int i;

    /* Use the provided seed to generate rho, rhoprime, and key */
    shake256(seedbuf, 3 * SEEDBYTES, seed, SEEDBYTES);
    rho = seedbuf;
    rhoprime = rho + SEEDBYTES;
    key = rhoprime + SEEDBYTES;

    /* Expand matrix */
    expand_mat(mat, rho);

    /* Sample short vectors s1 and s2 using rhoprime */
    polyvecl_uniform_eta(&s1, rhoprime, 0);
    polyveck_uniform_eta(&s2, rhoprime, L);

    /* Compute t = As1 + s2 */
    s1hat = s1;
    polyvecl_ntt(&s1hat);
    for (i = 0; i < K; ++i)
    {
        polyvecl_pointwise_acc_montgomery(&t1.vec[i], &mat[i], &s1hat);
        poly_invntt_tomont(&t1.vec[i]);
    }
    polyveck_add(&t1, &t1, &s2);

    /* Extract t1 and write public key */
    polyveck_caddq(&t1);
    polyveck_power2round(&t1, &t0, &t1);
    pack_pk(pk, rho, &t1);

    /* Hash rho and t1 to obtain tr */
    uint8_t buf[CRYPTO_PUBLICKEYBYTES];
    memcpy(buf, pk, CRYPTO_PUBLICKEYBYTES);
    shake256(tr, CRHBYTES, buf, CRYPTO_PUBLICKEYBYTES);

    /* Pack secret key */
    pack_sk(sk, rho, tr, key, &t0, &s1, &s2);

    /* Clean sensitive data */
    secure_erase(seedbuf, sizeof(seedbuf));
    secure_erase((void*)&s1, sizeof(s1));
    secure_erase((void*)&s1hat, sizeof(s1hat));
    secure_erase((void*)&s2, sizeof(s2));
    secure_erase((void*)&t0, sizeof(t0));
    secure_erase((void*)&t1, sizeof(t1));

    return 0;
}

int
pqcrystals_dilithium2_ref_publickey(uint8_t* pk, const uint8_t* sk)
{
    uint8_t seedbuf[3 * SEEDBYTES + 2 * CRHBYTES];
    uint8_t *rho, *tr, *key;
    polyvecl mat[K], s1, s1hat;
    polyveck t0, t1, s2;

    rho = seedbuf;
    tr = rho + SEEDBYTES;
    key = tr + SEEDBYTES;
    unpack_sk(rho, tr, key, &t0, &s1, &s2, sk);

    /* Expand matrix */
    polyvec_matrix_expand(mat, rho);

    /* Matrix-vector multiplication */
    s1hat = s1;
    polyvecl_ntt(&s1hat);
    polyvec_matrix_pointwise_montgomery(&t1, mat, &s1hat);
    polyveck_reduce(&t1);
    polyveck_invntt_tomont(&t1);

    /* Add error vector s2 */
    polyveck_add(&t1, &t1, &s2);

    /* Extract t1 and write public key */
    polyveck_caddq(&t1);
    polyveck_power2round(&t1, &t0, &t1);
    pack_pk(pk, rho, &t1);

    return 1;
}

SecretKey
generateSecretKey(KeyType type, Seed const& seed)
{
    if (type == KeyType::ed25519)
    {
        auto key = sha512Half_s(Slice(seed.data(), seed.size()));
        SecretKey sk{Slice{key.data(), key.size()}};
        secure_erase(key.data(), key.size());
        return sk;
    }

    if (type == KeyType::secp256k1)
    {
        auto key = detail::deriveDeterministicRootKey(seed);
        SecretKey sk{Slice{key.data(), key.size()}};
        secure_erase(key.data(), key.size());
        return sk;
    }

    if (type == KeyType::dilithium)
    {
        uint8_t pk[CRYPTO_PUBLICKEYBYTES];
        uint8_t buf[CRYPTO_SECRETKEYBYTES];
        auto key = sha512Half_s(Slice(seed.data(), seed.size()));
        pqcrystals_dilithium2_ref_keypair_seed(pk, buf, key.data());
        SecretKey sk{Slice{buf, CRYPTO_SECRETKEYBYTES}};
        secure_erase(buf, CRYPTO_SECRETKEYBYTES);
        return sk;
    }

    LogicError("generateSecretKey: unknown key type");
}

PublicKey
derivePublicKey(KeyType type, SecretKey const& sk)
{
    switch (type)
    {
        case KeyType::secp256k1: {
            secp256k1_pubkey pubkey_imp;
            if (secp256k1_ec_pubkey_create(
                    secp256k1Context(),
                    &pubkey_imp,
                    reinterpret_cast<unsigned char const*>(sk.data())) != 1)
                LogicError(
                    "derivePublicKey: secp256k1_ec_pubkey_create failed");

            unsigned char pubkey[33];
            std::size_t len = sizeof(pubkey);
            if (secp256k1_ec_pubkey_serialize(
                    secp256k1Context(),
                    pubkey,
                    &len,
                    &pubkey_imp,
                    SECP256K1_EC_COMPRESSED) != 1)
                LogicError(
                    "derivePublicKey: secp256k1_ec_pubkey_serialize failed");

            return PublicKey{Slice{pubkey, len}};
        }
        case KeyType::ed25519: {
            unsigned char buf[33];
            buf[0] = 0xED;
            ed25519_publickey(sk.data(), &buf[1]);
            return PublicKey(Slice{buf, sizeof(buf)});
        }
        case KeyType::dilithium: {
            uint8_t pk_data[CRYPTO_PUBLICKEYBYTES];
            if (pqcrystals_dilithium2_ref_publickey(pk_data, sk.data()) != 1)
                LogicError(
                    "derivePublicKey: secp256k1_ec_pubkey_serialize failed");

            return PublicKey{Slice{pk_data, CRYPTO_PUBLICKEYBYTES}};
        }
        default:
            LogicError("derivePublicKey: bad key type");
    };
}

std::pair<PublicKey, SecretKey>
generateKeyPair(KeyType type, Seed const& seed)
{
    switch (type)
    {
        case KeyType::secp256k1: {
            detail::Generator g(seed);
            return g(0);
        }
        case KeyType::ed25519: {
            auto const sk = generateSecretKey(type, seed);
            return {derivePublicKey(type, sk), sk};
        }
        case KeyType::dilithium: {
            auto const sk = generateSecretKey(type, seed);
            return {derivePublicKey(type, sk), sk};
        }
        default:
            throw std::invalid_argument("Unsupported key type");
    }
}

std::pair<PublicKey, SecretKey>
randomKeyPair(KeyType type)
{
    auto const sk = randomSecretKey(type);
    return {derivePublicKey(type, sk), sk};
}

template <>
std::optional<SecretKey>
parseBase58(TokenType type, std::string const& s)
{
    auto const result = decodeBase58Token(s, type);
    if (result.empty())
        return std::nullopt;
    if (result.size() != 32 && result.size() != 2560)
        return std::nullopt;
    return SecretKey(makeSlice(result));
}

}  // namespace xrpl

#pragma pop_macro("K")
#pragma pop_macro("L")
#pragma pop_macro("N")
#pragma pop_macro("S")
#pragma pop_macro("U")
#pragma pop_macro("D")
