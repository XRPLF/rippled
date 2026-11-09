#include <xrpl/protocol/PublicKey.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <ed25519.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <string>

namespace xrpl {

namespace {

/**
 * Check whether an Ed25519 signature is in canonical form.
 *
 * To enforce non-malleable signatures, RFC 8032 5.1.7 requires a verifier
 * to reject any signatures where S is not strictly less than L, the order
 * of the prime-order subgroup.
 *
 * Because ed25519-donna does not perform this check internally, we do it
 * ourselves, before validating the signature.
 *
 * @param sig The signature to check.
 * @return true if sig is exactly 64 bytes and its S component is less
 *         than L; false otherwise.
 */
bool
ed25519Canonical(Slice sig) noexcept
{
    // The order of the prime-order subgroup of Ed25519, in hexadecimal.
    static constexpr UInt256 ed25519Order{
        "1000000000000000000000000000000014DEF9DEA2F79CD65812631A5CF5D3ED"};

    if (sig.size() != 64)
        return false;

    // The second half of the signature is S, in little-endian. It is canonical
    // only if it is strictly less than the group order.
    std::array<std::uint8_t, UInt256::kBytes> s;
    std::ranges::reverse_copy(sig + 32, s.begin());

    return UInt256::fromRaw(s) < ed25519Order;
}

}  // namespace

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

PublicKey::PublicKey(Slice const& slice)
{
    if (slice.size() < kSize)
    {
        logicError(
            "PublicKey::PublicKey - Input slice cannot be an undersized "
            "buffer");
    }

    if (!publicKeyType(slice))
        logicError("PublicKey::PublicKey invalid type");
    std::memcpy(buf_, slice.data(), kSize);
}

PublicKey::PublicKey(PublicKey const& other)
{
    std::memcpy(buf_, other.buf_, kSize);
}

PublicKey&
PublicKey::operator=(PublicKey const& other)
{
    if (this != &other)
    {
        std::memcpy(buf_, other.buf_, kSize);
    }

    return *this;
}

//------------------------------------------------------------------------------

std::optional<KeyType>
publicKeyType(Slice const& slice) noexcept
{
    if (slice.size() == 33)
    {
        if (slice[0] == 0xED)
            return KeyType::Ed25519;

        if (slice[0] == kEcCompressedPrefixEvenY || slice[0] == kEcCompressedPrefixOddY)
            return KeyType::Secp256k1;
    }

    return std::nullopt;
}

bool
verifyDigest(
    PublicKey const& publicKey,
    UInt256 const& digest,
    Slice sig,
    bool mustBeFullyCanonical) noexcept
{
    if (publicKeyType(publicKey) != KeyType::Secp256k1)
        logicError("verifyDigest: secp256k1 required for digest signing");

    // FIXME: The check against data() is purely defensive and should be
    //        removed after Slice handles nullptr better.
    if (sig.empty() || sig.data() == nullptr)
        return false;

    // Strict DER parsing: an R or an S that is out-of-range (or 0) will parse,
    // but libsecp256k1 guarantees that such signatures will fail verification
    // with every message and every key in secp256k1_ecdsa_verify.
    secp256k1_ecdsa_signature signature;

    if (secp256k1_ecdsa_signature_parse_der(
            secp256k1Context(), &signature, sig.data(), sig.size()) != 1)
    {
        return false;
    }

    // We must *always* normalize the signature, ensuring that it is in lower-S
    // form. After this, the signature is fully canonical.
    if (bool const hadHighS =
            secp256k1_ecdsa_signature_normalize(secp256k1Context(), &signature, &signature) == 1;
        mustBeFullyCanonical && hadHighS)
    {
        return false;
    }

    secp256k1_pubkey pubkeyImp;

    if (secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubkeyImp, publicKey.data(), publicKey.size()) != 1)
    {
        return false;
    }

    return secp256k1_ecdsa_verify(secp256k1Context(), &signature, digest.data(), &pubkeyImp) == 1;
}

bool
verify(PublicKey const& publicKey, Slice m, Slice sig) noexcept
{
    if (auto const type = publicKeyType(publicKey))
    {
        if (*type == KeyType::Secp256k1)
            return verifyDigest(publicKey, sha512Half(m), sig);

        if (*type == KeyType::Ed25519)
        {
            if (!ed25519Canonical(sig))
                return false;

            // We prefix Ed25519 public keys with a 0xED byte to distinguish
            // them from secp256k1 keys, which we have to skip.
            return ed25519_sign_open(m.data(), m.size(), publicKey.data() + 1, sig.data()) == 0;
        }
    }
    return false;
}

NodeID
calcNodeID(PublicKey const& pk)
{
    static_assert(NodeID::kBytes == sizeof(RipeshaHasher::result_type));

    RipeshaHasher h;
    h(pk.data(), pk.size());
    return NodeID::fromRaw(static_cast<RipeshaHasher::result_type>(h));
}

}  // namespace xrpl
