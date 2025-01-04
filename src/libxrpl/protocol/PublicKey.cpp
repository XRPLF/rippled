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
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <ed25519.h>
#include "api.h"

#ifndef CRYPTO_PUBLICKEYBYTES
#define CRYPTO_PUBLICKEYBYTES pqcrystals_dilithium2_PUBLICKEYBYTES 
#endif

#ifndef crypto_sign_verify
#define crypto_sign_verify pqcrystals_dilithium2_ref_verify 
#endif

namespace ripple {

std::ostream&
operator<<(std::ostream& os, PublicKey const& pk)
{
    os << strHex(pk);
    return os;
}

template <>
std::optional<PublicKey>
parseBase58(TokenType type, std::string const& s)
{
    auto const result = decodeBase58Token(s, type);
    auto const pks = makeSlice(result);
    if (!publicKeyType(pks))
        return std::nullopt;
    return PublicKey(pks);
}

//------------------------------------------------------------------------------

// Parse a length-prefixed number
//  Format: 0x02 <length-byte> <number>
static std::optional<Slice>
sigPart(Slice& buf)
{
    if (buf.size() < 3 || buf[0] != 0x02)
        return std::nullopt;
    auto const len = buf[1];
    buf += 2;
    if (len > buf.size() || len < 1 || len > 33)
        return std::nullopt;
    // Can't be negative
    if ((buf[0] & 0x80) != 0)
        return std::nullopt;
    if (buf[0] == 0)
    {
        // Can't be zero
        if (len == 1)
            return std::nullopt;
        // Can't be padded
        if ((buf[1] & 0x80) == 0)
            return std::nullopt;
    }
    std::optional<Slice> number = Slice(buf.data(), len);
    buf += len;
    return number;
}

static std::string
sliceToHex(Slice const& slice)
{
    std::string s;
    if (slice[0] & 0x80)
    {
        s.reserve(2 * (slice.size() + 2));
        s = "0x00";
    }
    else
    {
        s.reserve(2 * (slice.size() + 1));
        s = "0x";
    }
    for (int i = 0; i < slice.size(); ++i)
    {
        s += "0123456789ABCDEF"[((slice[i] & 0xf0) >> 4)];
        s += "0123456789ABCDEF"[((slice[i] & 0x0f) >> 0)];
    }
    return s;
}

/** Determine whether a signature is canonical.
    Canonical signatures are important to protect against signature morphing
    attacks.
    @param vSig the signature data
    @param sigLen the length of the signature
    @param strict_param whether to enforce strictly canonical semantics

    @note For more details please see:
    https://xrpl.org/transaction-malleability.html
    https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
    https://github.com/sipa/bitcoin/commit/58bc86e37fda1aec270bccb3df6c20fbd2a6591c
*/
std::optional<ECDSACanonicality>
ecdsaCanonicality(Slice const& sig)
{
    using uint264 =
        boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
            264,
            264,
            boost::multiprecision::signed_magnitude,
            boost::multiprecision::unchecked,
            void>>;

    static uint264 const G(
        "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");

    // The format of a signature should be:
    // <30> <len> [ <02> <lenR> <R> ] [ <02> <lenS> <S> ]
    if ((sig.size() < 8) || (sig.size() > 72))
        return std::nullopt;
    if ((sig[0] != 0x30) || (sig[1] != (sig.size() - 2)))
        return std::nullopt;
    Slice p = sig + 2;
    auto r = sigPart(p);
    auto s = sigPart(p);
    if (!r || !s || !p.empty())
        return std::nullopt;

    uint264 R(sliceToHex(*r));
    if (R >= G)
        return std::nullopt;

    uint264 S(sliceToHex(*s));
    if (S >= G)
        return std::nullopt;

    // (R,S) and (R,G-S) are canonical,
    // but is fully canonical when S <= G-S
    auto const Sp = G - S;
    if (S > Sp)
        return ECDSACanonicality::canonical;
    return ECDSACanonicality::fullyCanonical;
}

static bool
ed25519Canonical(Slice const& sig)
{
    if (sig.size() != 64)
        return false;
    // Big-endian Order, the Ed25519 subgroup order
    std::uint8_t const Order[] = {
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0xDE, 0xF9, 0xDE, 0xA2, 0xF7,
        0x9C, 0xD6, 0x58, 0x12, 0x63, 0x1A, 0x5C, 0xF5, 0xD3, 0xED,
    };
    // Take the second half of signature
    // and byte-reverse it to big-endian.
    auto const le = sig.data() + 32;
    std::uint8_t S[32];
    std::reverse_copy(le, le + 32, S);
    // Must be less than Order
    return std::lexicographical_compare(S, S + 32, Order, Order + 32);
}

//------------------------------------------------------------------------------

// Constructor from Slice
PublicKey::PublicKey(Slice const& slice) {
    std::cout << "PublicKey constructor called." << std::endl;
    std::cout << "Input slice size: " << slice.size() << std::endl;

    // Determine the key type from the slice
    auto keyType = publicKeyType(slice);
    if (!keyType) {
        std::cout << "Invalid public key type detected." << std::endl;
        throw std::logic_error("PublicKey::PublicKey - Invalid public key type");
    }

    // Determine the expected size based on the key type
    std::size_t expectedSize = 0;
    switch (*keyType) {
        case KeyType::secp256k1:
            expectedSize = 33; // secp256k1 public keys are 33 bytes
            break;
        case KeyType::ed25519:
            expectedSize = 33; // ed25519 public keys are 33 bytes
            break;
        case KeyType::dilithium:
            expectedSize = CRYPTO_PUBLICKEYBYTES; // Dilithium public keys
            std::cout << "Verification of message successful using dilithium" << std::endl;
            break; // Add this break statement to prevent fallthrough
        default:
            std::cout << "Unknown key type detected." << std::endl;
            throw std::logic_error("PublicKey::PublicKey - Unknown key type");
    }

    std::cout << "Expected public key size: " << expectedSize << std::endl;

    // Validate the input slice size
    if (slice.size() < expectedSize) {
        std::cout << "Logic error: PublicKey::PublicKey - Input slice cannot be an undersized buffer" << std::endl;
        throw std::logic_error("PublicKey::PublicKey - Input slice cannot be an undersized buffer");
    } else if (slice.size() > expectedSize) {
        std::cout << "Logic error: PublicKey::PublicKey - Input slice cannot be an oversized buffer" << std::endl;
        throw std::logic_error("PublicKey::PublicKey - Input slice cannot be an oversized buffer");
    }

    // Allocate the buffer dynamically to match the key size
    keySize_ = expectedSize;
    buf_.resize(keySize_);

    // Copy the key data into the buffer
    std::memcpy(buf_.data(), slice.data(), keySize_);
}

// // Copy Constructor
// PublicKey::PublicKey(PublicKey const& other)
//     : buf_(other.buf_), keySize_(other.keySize_) {
//     std::cout << "PublicKey copy constructor called." << std::endl;
// }

// // Copy Assignment Operator
// PublicKey& PublicKey::operator=(PublicKey const& other) {
//     if (this != &other) {
//         std::cout << "PublicKey copy assignment operator called." << std::endl;
//         buf_ = other.buf_; // Copy the buffer
//         keySize_ = other.keySize_; // Copy the key size
//     }
//     return *this;
// }

//------------------------------------------------------------------------------

std::optional<KeyType>
publicKeyType(Slice const& slice)
{
    if (slice.size() == 33)
    {
        if (slice[0] == 0xED)
            return KeyType::ed25519;

        if (slice[0] == 0x02 || slice[0] == 0x03)
            return KeyType::secp256k1;
    }
    else if (slice.size() == CRYPTO_PUBLICKEYBYTES)
    {
        return KeyType::dilithium;
    }
    

    return std::nullopt;
}

bool
verifyDigest(
    PublicKey const& publicKey,
    uint256 const& digest,
    Slice const& sig,
    bool mustBeFullyCanonical) noexcept
{
    auto const type = publicKeyType(publicKey);
    if (!type)
    {
        LogicError("verifyDigest: unknown public key type");
        return false;
    }

    switch (*type)
    {
        case KeyType::secp256k1:
        {
            std::cout << "Verifying digest, public key type: secp256k1" << std::endl;
            auto const canonicality = ecdsaCanonicality(sig);
            if (!canonicality)
                return false;
            if (mustBeFullyCanonical &&
                (*canonicality != ECDSACanonicality::fullyCanonical))
                return false;

            secp256k1_pubkey pubkey_imp;
            if (secp256k1_ec_pubkey_parse(
                    secp256k1Context(),
                    &pubkey_imp,
                    reinterpret_cast<unsigned char const*>(publicKey.data()),
                    publicKey.size()) != 1)
                return false;

            secp256k1_ecdsa_signature sig_imp;
            if (secp256k1_ecdsa_signature_parse_der(
                    secp256k1Context(),
                    &sig_imp,
                    reinterpret_cast<unsigned char const*>(sig.data()),
                    sig.size()) != 1)
                return false;
            if (*canonicality != ECDSACanonicality::fullyCanonical)
            {
                secp256k1_ecdsa_signature sig_norm;
                if (secp256k1_ecdsa_signature_normalize(
                        secp256k1Context(), &sig_norm, &sig_imp) != 1)
                    return false;
                return secp256k1_ecdsa_verify(
                           secp256k1Context(),
                           &sig_norm,
                           reinterpret_cast<unsigned char const*>(digest.data()),
                           &pubkey_imp) == 1;
            }
            return secp256k1_ecdsa_verify(
                       secp256k1Context(),
                       &sig_imp,
                       reinterpret_cast<unsigned char const*>(digest.data()),
                       &pubkey_imp) == 1;
        }
        case KeyType::ed25519:
        {
            std::cout << "Verifying digest, public key type: ed25519" << std::endl;
            if (!ed25519Canonical(sig))
                return false;

            return ed25519_sign_open(
                       digest.data(), digest.size(), publicKey.data() + 1, sig.data()) == 0;
        }
        case KeyType::dilithium:
        {
            std::cout << "Verifying digest, public key type: Dilithium" << std::endl;
            return crypto_sign_verify(
                       sig.data(), sig.size(), digest.data(), digest.size(), publicKey.data()) == 0;
        }
        default:
            LogicError("verifyDigest: invalid public key type");
            return false;
    }
}

bool
verify(
    PublicKey const& publicKey,
    Slice const& m,
    Slice const& sig,
    bool mustBeFullyCanonical) noexcept
{
    auto const type = publicKeyType(publicKey);
    if (!type)
    {
        LogicError("verify: unknown public key type");
        return false;
    }

    switch (*type)
    {
        case KeyType::secp256k1:
        {
            std::cout << "Verifying message, public key type: secp256k1" << std::endl;
            return verifyDigest(
                publicKey, sha512Half(m), sig, mustBeFullyCanonical);
        }
        case KeyType::ed25519:
        {
            std::cout << "Verifying message, public key type: ed25519" << std::endl;
            if (!ed25519Canonical(sig))
                return false;

            // We internally prefix Ed25519 keys with a 0xED
            // byte to distinguish them from secp256k1 keys
            // so when verifying the signature, we need to
            // first strip that prefix.
            return ed25519_sign_open(
                       m.data(), m.size(), publicKey.data() + 1, sig.data()) == 0;
        }
        case KeyType::dilithium:
        {
            std::cout << "Verifying message, public key type: Dilithium" << std::endl;
            return crypto_sign_verify(
                       sig.data(), sig.size(), m.data(), m.size(), publicKey.data()) == 0;
            std::cout << "Verification of message succesfull using dilithium" << std::endl;
            
        }
        default:
            LogicError("verify: invalid public key type");
            return false;
    }
}

NodeID
calcNodeID(PublicKey const& pk)
{
    static_assert(NodeID::bytes == sizeof(ripesha_hasher::result_type));

    ripesha_hasher h;
    h(pk.data(), pk.size());
    return NodeID{static_cast<ripesha_hasher::result_type>(h)};
}

}  // namespace ripple
