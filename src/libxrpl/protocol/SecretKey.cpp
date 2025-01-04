//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ed25519.h>
#include "api.h"
#include <iostream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <iomanip>
#include <sstream>


//Define the dilithium functions and sizes with respect to functions named here
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

#ifndef crypto_sign_verify
#define crypto_sign_verify pqcrystals_dilithium2_ref_verify 
#endif

#ifndef crypto_sign_open
#define crypto_sign_open pqcrystals_dilithium2_ref_open 
#endif

#ifndef crypto_keypair_seed
#define crypto_keypair_seed pqcrystals_dilithium2_ref_keypair_seed 
#endif

namespace ripple {

// Securely erase memory
void secure_erase(std::uint8_t* data, std::size_t size) {
    if (data) {
        std::memset(data, 0, size);
    }
}


// Constructor with KeyType
SecretKey::SecretKey(KeyType type, Slice const& slice) {
    // Determine the key size based on the KeyType
    if (type == KeyType::secp256k1) {
        keySize_ = 32; // secp256k1 keys are 32 bytes
    } else if (type == KeyType::dilithium) {
        keySize_ = 2528; // Dilithium keys are 2528 bytes
    } else {
        throw std::logic_error("SecretKey::SecretKey: unsupported KeyType");
    }

    // Validate the input slice size
    if (slice.size() != keySize_) {
        throw std::logic_error("SecretKey::SecretKey: invalid key size for the given KeyType");
    }

    // Allocate the buffer dynamically to match the key size
    buf_.resize(keySize_);

    // Copy the key data into the buffer
    std::memcpy(buf_.data(), slice.data(), keySize_);
}

// Constructor from std::vector
SecretKey::SecretKey(std::vector<std::uint8_t> const& data) {
    if (data.size() != 32 && data.size() != 2528) {
        throw std::logic_error("SecretKey::SecretKey: invalid size");
    }
    keySize_ = data.size(); // Set the key size
    buf_ = std::vector<std::uint8_t>(data.begin(), data.end()); // Copy the key data
}

// Constructor from Slice
SecretKey::SecretKey(Slice const& slice) {
    if (slice.size() != 32 && slice.size() != 2528) {
        throw std::logic_error("SecretKey::SecretKey: invalid size");
    }
    keySize_ = slice.size(); // Set the key size
    buf_.resize(keySize_); // Allocate memory dynamically
    std::memcpy(buf_.data(), slice.data(), keySize_); // Copy the key data
}

std::string toHexString(const std::uint8_t* data, std::size_t size) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < size; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
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

Buffer signDigest(PublicKey const& pk, SecretKey const& sk, uint256 const& digest)
{
    std::cout << "Signing digest..." << std::endl;
    if (publicKeyType(pk.slice()) != KeyType::secp256k1 && publicKeyType(pk.slice()) != KeyType::dilithium)
        LogicError("sign: secp256k1 or Dilithium required for digest signing");
    
    if (publicKeyType(pk.slice()) == KeyType::secp256k1) {
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
    } else if (publicKeyType(pk.slice()) == KeyType::dilithium) {
        std::cout << "Signing digest with Dilithium" << std::endl;
        uint8_t dilithium_sig[CRYPTO_BYTES];
        size_t dilithium_siglen;
        crypto_sign_signature(dilithium_sig, &dilithium_siglen, digest.data(), digest.size(), sk.data());
        std::cout << "Signing Digest done with digest and Dilithium Signature Length: " << dilithium_siglen << std::endl;
        
        // Verify the signature
        if (crypto_sign_verify(dilithium_sig, dilithium_siglen, digest.data(), digest.size(), pk.data())) {
            std::cerr << "Dilithium Signature Verification Failed" << std::endl;
            LogicError("signDigest: Dilithium Signature Verification Failed");
        }
        return Buffer{dilithium_sig, dilithium_siglen};
    }
    LogicError("signDigest: unknown key type");
}

Buffer sign(PublicKey const& pk, SecretKey const& sk, Slice const& m)
{
    std::cout << "Signing message..." << std::endl;
    auto const type = publicKeyType(pk.slice());
    if (!type)
        LogicError("sign: invalid type");
    switch (*type)
    {
        case KeyType::ed25519: {
            std::cout << "Signing using ed25519" << std::endl;
            const size_t ed25519_siglen = 64; // Ed25519 signature length
            Buffer b(ed25519_siglen);
            ed25519_sign(
                m.data(), m.size(), sk.data(), pk.data() + 1, b.data());

            // Debugging statements
            std::cout << "Signature (ed25519): " << toHexString(b.data(), ed25519_siglen) << std::endl;
            std::cout << "Signature Length (ed25519) : " << ed25519_siglen << " bytes" << std::endl;

            return b;
        }
        case KeyType::secp256k1: {
            std::cout << "Signing using Secp256k1..." << std::endl;
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

            // Debugging statements
            std::cout << "Signature (Secp256k1): " << toHexString(sig, len) << std::endl;
            std::cout << "Signature Length (Secp256k1): " << len << " bytes" << std::endl;

            return Buffer{sig, len};
        }
        case KeyType::dilithium: {
            std::cout << "Signing message using dilithium" << std::endl;
            uint8_t dilithium_sig[CRYPTO_BYTES];
            size_t dilithium_siglen;
            crypto_sign_signature(dilithium_sig, &dilithium_siglen, m.data(), m.size(), sk.data());

            // Debugging statements
            // std::cout << "Signature Dilthium: " << toHexString(dilithium_sig, dilithium_siglen) << std::endl;
            std::cout << "Signature Length (Dilithium): " << dilithium_siglen << " bytes" << std::endl;

            // Verify the Signature
            // int verify_result = crypto_sign_verify(dilithium_sig, dilithium_siglen, m.data(), m.size(), pk.data());
            // if (verify_result != 0) {
            //     std::cerr << "Dilithium signature verification failed with error code: " << verify_result << std::endl;
            //     LogicError("sign: Dilithium Signature Verification failed");
            // }
            return Buffer{dilithium_sig, dilithium_siglen};
        }
        default:
            LogicError("sign: invalid type");
    }
}

// Function to generate a secp256k1 secret key
SecretKey randomSecp256k1SecretKey() {
    std::cout << "Using Secp256k1" << std::endl;
    std::uint8_t buf[32];
    beast::rngfill(buf, sizeof(buf), crypto_prng());
    
    // std::cout << "Secret Key: " << toHexString(buf, sizeof(buf)) << std::endl;
    std::cout << "Length of Secret Key: " << sizeof(buf) << " bytes" << std::endl;
    SecretKey sk(Slice{buf, sizeof(buf)});
    secure_erase(buf, sizeof(buf));
    return sk;
}

// Function to generate a Dilithium secret key
SecretKey randomDilithiumSecretKey() {
    std::cout << "randomDilithiumSecretKey() called" << std::endl;
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);
        
    // std::cout << "Secret Key: " << toHexString(sk, CRYPTO_SECRETKEYBYTES) << std::endl;
    std::cout << "Length of Secret Key: (dilithium) " << CRYPTO_SECRETKEYBYTES << " bytes" << std::endl;
    return SecretKey(Slice{sk, CRYPTO_SECRETKEYBYTES});
}

SecretKey
generateSecretKey(KeyType type, Seed const& seed)
{
    if (type == KeyType::ed25519)
    {
        std::cout << "Generating SecretKey using ed25519..." << std::endl;
        auto key = sha512Half_s(Slice(seed.data(), seed.size()));
        SecretKey sk{Slice{key.data(), key.size()}};

        // Debugging statements
        std::cout << "Secret Key(ed25519): " << toHexString(key.data(), key.size()) << std::endl;
        std::cout << "Secret Key Size (ed25519): " << key.size() << " bytes" << std::endl;

        secure_erase(key.data(), key.size());
        return sk;
    }

    if (type == KeyType::secp256k1)
    {
        std::cout << "Generating SecretKey using secp256k1..." << std::endl;
        auto key = detail::deriveDeterministicRootKey(seed);
        SecretKey sk{Slice{key.data(), key.size()}};

        // Debugging statements
        std::cout << "Secret Key (secp256k1) : " << toHexString(key.data(), key.size()) << std::endl;
        std::cout << "Secret Key Size (secp256k1): " << key.size() << " bytes" << std::endl;

        secure_erase(key.data(), key.size());
        return sk;
    }

    if (type == KeyType::dilithium)
    {
        std::cout << "Generating SecretKey using Dilithium..." << std::endl;
        uint8_t pk[CRYPTO_PUBLICKEYBYTES];
        uint8_t sk_temp[CRYPTO_SECRETKEYBYTES];

        // Generate the key pair from the seed
        if (crypto_keypair_seed(pk, sk_temp, seed.data()) != 0) {
            throw std::runtime_error("Dilithium key pair generation failed");
        }

        SecretKey sk{Slice{sk_temp, CRYPTO_SECRETKEYBYTES}};

        // Debugging statements
        // std::cout << "Secret Key (dilithium): " << toHexString(sk, CRYPTO_SECRETKEYBYTES) << std::endl;
        std::cout << "Secret Key Size (dilithium): generateKeypair() " << CRYPTO_SECRETKEYBYTES << " bytes" << std::endl;

        // Securely erase the public key if not needed
        secure_erase(pk, CRYPTO_PUBLICKEYBYTES);

        return sk;
    }

    LogicError("generateSecretKey: unknown key type");
}

PublicKey derivePublicKey(KeyType type, SecretKey const& sk)
{
    switch (type)
    {
        case KeyType::secp256k1: {
            std::cout << "Deriving PublicKey using secp256k1..." << std::endl;
            secp256k1_pubkey pubkey_imp;
            if (secp256k1_ec_pubkey_create(
                    secp256k1Context(),
                    &pubkey_imp,
                    reinterpret_cast<unsigned char const*>(sk.data())) != 1)
                LogicError("derivePublicKey: secp256k1_ec_pubkey_create failed");

            unsigned char pubkey[33];
            std::size_t len = sizeof(pubkey);
            if (secp256k1_ec_pubkey_serialize(
                    secp256k1Context(),
                    pubkey,
                    &len,
                    &pubkey_imp,
                    SECP256K1_EC_COMPRESSED) != 1)
                LogicError("derivePublicKey: secp256k1_ec_pubkey_serialize failed");

            std::cout << "Public Key Length (secp256k1): " << len << " bytes" << std::endl;

            return PublicKey{Slice{pubkey, len}};
        }
        case KeyType::ed25519: {
            std::cout << "Deriving PublicKey using ed25519..." << std::endl;
            unsigned char buf[33];
            buf[0] = 0xED;
            ed25519_publickey(sk.data(), &buf[1]);

            std::cout << "Public Key Length (ed25519): " << sizeof(buf) << " bytes" << std::endl;

            return PublicKey(Slice{buf, sizeof(buf)});
        }
        default: {
            int expectedKeySize = 0;
            if (type == KeyType::secp256k1) {
                expectedKeySize = 32;
            } else if (type == KeyType::ed25519) {
                expectedKeySize = 32;
            } else {
                expectedKeySize = -1; // Unknown key type
            }

            std::ostringstream oss;
            oss << "derivePublicKey: bad key type. "
                << "Expected key size: 32" 
                << ", Actual key size: " << sk.size();
            LogicError(oss.str());
        }
    }
}

PublicKey derivePublicKey(KeyType type, SecretKey const& sk, Seed const& seed)
{
    if (type != KeyType::dilithium) {
        LogicError("derivePublicKey: unsupported key type with seed");
    }

    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk_buffer[CRYPTO_SECRETKEYBYTES];
    

    // Debugging statement before key derivation
    std::cout << "derivePublicKey() using Dilithium..." << std::endl;

    if (pqcrystals_dilithium2_ref_keypair_seed(pk, sk_buffer, seed.data()) != 0) {
        throw std::runtime_error("derivePublicKey: Dilithium public key derivation failed");
    }

    // Debugging statements after key derivation
    // std::cout << "Public Key (Dilithium): " << toHexString(pk, CRYPTO_PUBLICKEYBYTES) << std::endl;
    // std::cout << "Public Key (Dilithium): " << toHexString(pk, CRYPTO_PUBLICKEYBYTES) << std::endl;
    std::cout << "derivePublicKey Length (Dilithium): " << CRYPTO_PUBLICKEYBYTES << " bytes" << std::endl;

    return PublicKey{Slice{pk, CRYPTO_PUBLICKEYBYTES}};
}

std::pair<PublicKey, SecretKey>
generateKeyPair(KeyType type, Seed const& seed)
{
    switch (type)
    {
        case KeyType::secp256k1: {
            detail::Generator g(seed);
            auto keyPair = g(0);
            // Debugging statements
            std::cout << "generateKeypair Using secp256k1..." << std::endl;
            std::cout << "Public Key (secp256k1): " << toHexString(keyPair.first.data(), keyPair.first.size()) << std::endl;
           
            return keyPair;
        }
        case KeyType::ed25519: {
            std::cout << "generateKeypair Using ed25519..." << std::endl;
            auto const sk = generateSecretKey(type, seed);
            PublicKey pk = derivePublicKey(type, sk);

            // Debugging statements
            std::cout << "Public Key (ed25519): " << toHexString(pk.data(), pk.size()) << std::endl;
            std::cout << "Secret Key(ed25519): " << toHexString(sk.data(), sk.size()) << std::endl;
            std::cout << "Secret Key Length (ed25519): " << sk.size() << " bytes" << std::endl;

            return {pk, sk};
        }
        case KeyType::dilithium: {
            std::cout << "generateKeypair Using Dilithium..." << std::endl;
            auto const sk = generateSecretKey(type, seed);
            PublicKey pk = derivePublicKey(type, sk ,seed);



            // Debugging statements

            std::cout << "Secret Key Length (dilithium): generateKeyPair" << sk.size() << " bytes" << std::endl;

            // Return the key pair
            return {pk, sk};
        }

        default:
            throw std::invalid_argument("Unsupported key type");
    }
}

// std::pair<PublicKey, SecretKey>
// randomKeyPair(KeyType type)
// {
//     auto const sk = randomSecretKey();
//     return {derivePublicKey(type, sk), sk};
// }

// Added randomKeyPair for dilithium as well along with secp256k1.
std::pair<PublicKey, SecretKey> randomKeyPair(KeyType type)
{
    if (type == KeyType::secp256k1) {
        std::cout << "randomKeyPair using secp256k1" << std::endl;
        auto const sk = randomSecp256k1SecretKey();
        auto const pk = derivePublicKey(KeyType::secp256k1, sk);

        // Debugging statements
        std::cout << "Secret Key (secp256k1): " << toHexString(sk.data(), sk.size()) << std::endl;
        std::cout << "Public Key (secp256k1): " << toHexString(pk.data(), pk.size()) << std::endl;
       

        return {pk, sk};

    } else if (type == KeyType::dilithium) {
        std::cout << "randomKeyPair using Dilithium" << std::endl;
        auto const sk = randomDilithiumSecretKey();
        auto const pk = derivePublicKey(KeyType::dilithium, sk, randomSeed());

        // Debugging statements
        // std::cout << "Secret Key (dilithium) randomKeyPair(): " << toHexString(sk.data(), sk.size()) << std::endl;
        // std::cout << "Public Key (dilithium) randomKeyPair(): " << toHexString(pk.data(), pk.size()) << std::endl;
        std::cout << "Secret Key Length (dilithium) randomKeyPair(): " << sk.size() << " bytes" << std::endl;
        std::cout << "Public Key Length (dilithium) randomKeyPair(): " << pk.size() << " bytes" << std::endl;

        return {pk, sk};

    } else {
        throw std::invalid_argument("randomKeyPair: unknown key type");
    }
}

template <>
std::optional<SecretKey>
parseBase58(TokenType type, std::string const& s)
{
    auto const result = decodeBase58Token(s, type);
    if (result.empty())
        return std::nullopt;
    //Added size for Dilithium key size with secp256k1 as well.
    if (result.size() != 32 && result.size() != 2528)
        return std::nullopt;
    return SecretKey(makeSlice(result));
}

}  // namespace ripple
