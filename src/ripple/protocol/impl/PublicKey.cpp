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

#include <ripple/basics/contract.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/impl/secp256k1.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <ed25519-donna/ed25519.h>
#include <type_traits>

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
    https://ripple.com/wiki/Transaction_Malleability
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

PublicKey::PublicKey(Slice const& slice)
{
    if (!publicKeyType(slice))
        LogicError("PublicKey::PublicKey invalid type");
    size_ = slice.size();
    std::memcpy(buf_, slice.data(), size_);
}

PublicKey::PublicKey(PublicKey const& other) : size_(other.size_)
{
    if (size_)
        std::memcpy(buf_, other.buf_, size_);
};

PublicKey&
PublicKey::operator=(PublicKey const& other)
{
    size_ = other.size_;
    if (size_)
        std::memcpy(buf_, other.buf_, size_);
    return *this;
}

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

    return std::nullopt;
}

bool
verifyDigest(
    PublicKey const& publicKey,
    uint256 const& digest,
    Slice const& sig,
    bool mustBeFullyCanonical) noexcept
{
    if (publicKeyType(publicKey) != KeyType::secp256k1)
        LogicError("sign: secp256k1 required for digest signing");
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

bool
verify(
    PublicKey const& publicKey,
    Slice const& m,
    Slice const& sig,
    bool mustBeFullyCanonical) noexcept
{
    if (auto const type = publicKeyType(publicKey))
    {
        if (*type == KeyType::secp256k1)
        {
            return verifyDigest(
                publicKey, sha512Half(m), sig, mustBeFullyCanonical);
        }
        else if (*type == KeyType::ed25519)
        {
            if (!ed25519Canonical(sig))
                return false;

            // We internally prefix Ed25519 keys with a 0xED
            // byte to distinguish them from secp256k1 keys
            // so when verifying the signature, we need to
            // first strip that prefix.
            return ed25519_sign_open(
                       m.data(), m.size(), publicKey.data() + 1, sig.data()) ==
                0;
        }
    }
    return false;
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
