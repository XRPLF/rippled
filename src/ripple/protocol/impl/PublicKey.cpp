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

#include <BeastConfig.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/impl/secp256k1.h>
#include <ripple/basics/contract.h>
#include <beast/ByteOrder.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <ed25519-donna/ed25519.h>
#include <type_traits>

namespace ripple {

using uint264 = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<
        264, 264, boost::multiprecision::signed_magnitude,
            boost::multiprecision::unchecked, void>>;

template<>
boost::optional<PublicKey>
parseBase58 (TokenType type, std::string const& s)
{
    auto const result =
        decodeBase58Token(s, type);
    if (result.empty())
        return boost::none;
    if (result.size() != 33)
        return boost::none;
    return PublicKey(makeSlice(result));
}

//------------------------------------------------------------------------------

// Parse a length-prefixed number
//  Format: 0x02 <length-byte> <number>
static
boost::optional<Slice>
sigPart (Slice& buf)
{
    if (buf.size() < 3 || buf[0] != 0x02)
        return boost::none;
    auto const len = buf[1];
    buf += 2;
    if (len > buf.size() || len < 1 || len > 33)
        return boost::none;
    // Can't be negative
    if ((buf[0] & 0x80) != 0)
        return boost::none;
    if (buf[0] == 0)
    {
        // Can't be zero
        if (len == 1)
            return boost::none;
        // Can't be padded
        if ((buf[1] & 0x80) == 0)
            return boost::none;
    }
    boost::optional<Slice> number =
        Slice(buf.data(), len);
    buf += len;
    return number;
}

template <std::size_t N>
void
swizzle (void* p);

template<>
void
swizzle<4>(void* p)
{
    (*reinterpret_cast<std::uint32_t*>(p))=
        beast::ByteOrder::swapIfLittleEndian(
            *reinterpret_cast<std::uint32_t*>(p));
}

template<>
void
swizzle<8>(void* p)
{
    (*reinterpret_cast<std::uint64_t*>(p))=
        beast::ByteOrder::swapIfLittleEndian(
            *reinterpret_cast<std::uint64_t*>(p));
}

template <class Number>
static
void
load (Number& mp, Slice const& buf)
{
    assert(buf.size() != 0);
    auto& b = mp.backend();         // backend
    auto const a = &b.limbs()[0];   // limb array
    using Limb = std::decay_t<
        decltype(a[0])>;            // word type
    b.resize((buf.size() + sizeof(Limb) - 1) / sizeof(Limb), 1);
    std::memset(&a[0], 0,
        b.size() * sizeof(Limb));   // zero fill
    auto n =
        buf.size() / sizeof(Limb);
    auto s = reinterpret_cast<Limb const*>(
        buf.data() + buf.size() - sizeof(Limb));
    auto d = a;
    while(n--)
    {
        *d = *s;
        swizzle<sizeof(Limb)>(d);
        d++;
        s--;
    }
    auto const r =
        buf.size() % sizeof(Limb);
    if (r > 0)
    {
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(d) + sizeof(Limb) - r,
                buf.data(), r);
        swizzle<sizeof(Limb)>(d);
    }
}

static
std::string
sliceToHex (Slice const& slice)
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
    for(int i = 0; i < slice.size(); ++i)
    {
        s += "0123456789ABCDEF"[((slice[i]&0xf0)>>4)];
        s += "0123456789ABCDEF"[((slice[i]&0x0f)>>0)];
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
boost::optional<ECDSACanonicality>
ecdsaCanonicality (Slice const& sig)
{
    static uint264 const G(
        "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");

    // The format of a signature should be:
    // <30> <len> [ <02> <lenR> <R> ] [ <02> <lenS> <S> ]
    if ((sig.size() < 8) || (sig.size() > 72))
        return boost::none;
    if ((sig[0] != 0x30) || (sig[1] != (sig.size() - 2)))
        return boost::none;
    Slice p = sig + 2;
    auto r = sigPart(p);
    auto s = sigPart(p);
    if (! r || ! s || ! p.empty())
        return boost::none;
#if 0
    uint264 R;
    uint264 S;
    load(R, *r);
    load(S, *s);
#else
    uint264 R(sliceToHex(*r));
    uint264 S(sliceToHex(*s));
#endif

    if (R >= G)
        return boost::none;
    if (S >= G)
        return boost::none;
    // (R,S) and (R,G-S) are canonical,
    // but is fully canonical when S <= G-S
    auto const Sp = G - S;
    if (S > Sp)
        return ECDSACanonicality::canonical;
    return ECDSACanonicality::fullyCanonical;
}

static
bool
ed25519Canonical (Slice const& sig)
{
    if (sig.size() != 64)
        return false;
    // Big-endian Order, the Ed25519 subgroup order
    std::uint8_t const Order[] =
    {
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x14, 0xDE, 0xF9, 0xDE, 0xA2, 0xF7, 0x9C, 0xD6,
        0x58, 0x12, 0x63, 0x1A, 0x5C, 0xF5, 0xD3, 0xED,
    };
    // Take the second half of signature
    // and byte-reverse it to big-endian.
    auto const le = sig.data() + 32;
    std::uint8_t S[32];
    std::reverse_copy(le, le + 32, S);
    // Must be less than Order
    return std::lexicographical_compare(
        S, S + 32, Order, Order + 32);
}

//------------------------------------------------------------------------------

PublicKey::PublicKey (Slice const& slice)
{
    if(! publicKeyType(slice))
        LogicError("PublicKey::PublicKey invalid type");
    size_ = slice.size();
    std::memcpy(buf_, slice.data(), slice.size());
}

PublicKey::PublicKey (PublicKey const& other)
    : size_ (other.size_)
{
    std::memcpy(buf_, other.buf_, size_);
};

PublicKey&
PublicKey::operator=(
    PublicKey const& other)
{
    size_ = other.size_;
    std::memcpy(buf_, other.buf_, size_);
    return *this;
}

KeyType
PublicKey::type() const
{
    auto const result =
        publicKeyType(Slice{ buf_, size_ });
    if (! result)
        LogicError("PublicKey::type: invalid type");
    return *result;
}

bool
PublicKey::verify (Slice const& m,
    Slice const& sig, bool mustBeFullyCanonical) const
{
    switch(type())
    {
    case KeyType::secp256k1:
    {
        auto const digest = sha512Half(m);
        auto const canonicality = ecdsaCanonicality(sig);
        if (! canonicality)
            return false;
        if (mustBeFullyCanonical && canonicality !=
                ECDSACanonicality::fullyCanonical)
            return false;
        return secp256k1_ecdsa_verify(
            secp256k1Context(), secpp(digest.data()),
                secpp(sig.data()), sig.size(),
                    secpp(buf_), size_) == 1;
    }
    default:
    case KeyType::ed25519:
    {
        if (! ed25519Canonical(sig))
            return false;
        return ed25519_sign_open(
            m.data(), m.size(), buf_ + 1,
                sig.data()) == 0;
    }
    }
}

//------------------------------------------------------------------------------

boost::optional<KeyType>
publicKeyType (Slice const& slice)
{
    if (slice.size() == 33 &&
            slice[0] == 0xED)
        return KeyType::ed25519;
    if (slice.size() == 33 &&
        (slice[0] == 0x02 ||
            slice[0] == 0x03))
        return KeyType::secp256k1;
    return boost::none;
}

bool
verify (PublicKey const& pk,
    Slice const& m, Slice const& sig)
{
    switch(pk.type())
    {
    default:
    case KeyType::secp256k1:
    {
        sha512_half_hasher h;
        h(m.data(), m.size());
        auto const digest =
            sha512_half_hasher::result_type(h);
        return secp256k1_ecdsa_verify(
            secp256k1Context(), digest.data(),
                sig.data(), sig.size(),
                    pk.data(), pk.size()) == 1;
    }
    case KeyType::ed25519:
    {
        if (sig.size() != 64)
            return false;
        return ed25519_sign_open(m.data(),
            m.size(), pk.data(), sig.data()) == 0;
    }
    }
}

NodeID
calcNodeID (PublicKey const& pk)
{
    ripesha_hasher h;
    h(pk.data(), pk.size());
    auto const digest = static_cast<
        ripesha_hasher::result_type>(h);
    static_assert(NodeID::bytes ==
        sizeof(ripesha_hasher::result_type), "");
    NodeID result;
    std::memcpy(result.data(),
        digest.data(), digest.size());
    return result;
}

} // ripple

