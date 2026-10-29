#include <xrpl/protocol/PublicKey.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <boost/multiprecision/number.hpp>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include <ed25519.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

namespace xrpl {

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
    if ((slice[0] & 0x80) != 0)
    {
        s.reserve(2 * (slice.size() + 2));
        s = "0x00";
    }
    else
    {
        s.reserve(2 * (slice.size() + 1));
        s = "0x";
    }
    for (std::uint8_t const byte : slice)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        s += kHex[((byte & 0xf0) >> 4)];
        s += kHex[((byte & 0x0f) >> 0)];
    }
    return s;
}

/**
 * Determine whether a signature is canonical.
 * Canonical signatures are important to protect against signature morphing
 * attacks.
 * @param vSig the signature data
 * @param sigLen the length of the signature
 * @param strict_param whether to enforce strictly canonical semantics
 *
 * @note For more details please see:
 * https://xrpl.org/transaction-malleability.html
 * https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
 * https://github.com/sipa/bitcoin/commit/58bc86e37fda1aec270bccb3df6c20fbd2a6591c
 */
std::optional<ECDSACanonicality>
ecdsaCanonicality(Slice const& sig)
{
    using UInt264 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
        264,
        264,
        boost::multiprecision::signed_magnitude,
        boost::multiprecision::unchecked,
        void>>;

    static UInt264 const kG(
        "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");  // NOLINT(readability-identifier-naming)

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

    UInt264 const rNum(sliceToHex(*r));
    if (rNum >= kG)
        return std::nullopt;

    UInt264 const sNum(sliceToHex(*s));
    if (sNum >= kG)
        return std::nullopt;

    // (R,S) and (R,G-S) are canonical,
    // but is fully canonical when S <= G-S
    auto const Sp = kG - sNum;  // NOLINT(readability-identifier-naming)
    if (sNum > Sp)
        return ECDSACanonicality::Canonical;
    return ECDSACanonicality::FullyCanonical;
}

static bool
ed25519Canonical(Slice const& sig)
{
    if (sig.size() != 64)
        return false;
    // Big-endian Order, the Ed25519 subgroup order
    // NOLINTNEXTLINE(readability-identifier-naming)
    std::uint8_t const Order[] = {
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0xDE, 0xF9, 0xDE, 0xA2, 0xF7,
        0x9C, 0xD6, 0x58, 0x12, 0x63, 0x1A, 0x5C, 0xF5, 0xD3, 0xED,
    };
    // Take the second half of signature
    // and byte-reverse it to big-endian.
    auto const le = sig.data() + 32;
    std::uint8_t S[32];  // NOLINT(readability-identifier-naming)
    std::reverse_copy(le, le + 32, S);
    // Must be less than Order
    return std::lexicographical_compare(S, S + 32, Order, Order + 32);
}

//------------------------------------------------------------------------------

PublicKey::PublicKey(Slice const& slice)
{
    if (slice.size() > kMaxSize)
    {
        logicError(
            "PublicKey::PublicKey - Input slice cannot be an oversized "
            "buffer");
    }

    if (!publicKeyType(slice))
        logicError("PublicKey::PublicKey invalid type");
    size_ = slice.size();
    std::memcpy(buf_, slice.data(), size_);
}

PublicKey::PublicKey(PublicKey const& other) : size_(other.size_)
{
    std::memcpy(buf_, other.buf_, size_);
}

PublicKey&
PublicKey::operator=(PublicKey const& other)
{
    if (this != &other)
    {
        size_ = other.size_;
        std::memcpy(buf_, other.buf_, size_);
    }

    return *this;
}

//------------------------------------------------------------------------------

std::optional<KeyType>
publicKeyType(Slice const& slice)
{
    if (slice.size() == 33)
    {
        if (slice[0] == 0xED)
            return KeyType::Ed25519;

        if (slice[0] == kEcCompressedPrefixEvenY || slice[0] == kEcCompressedPrefixOddY)
            return KeyType::Secp256k1;
    }

    if (slice.size() == 65 && slice[0] == 0xF6)
        return KeyType::P256;

    return std::nullopt;
}

std::optional<KeyType>
enabledPublicKeyType(Slice const& slice, Rules const& rules)
{
    auto const type = publicKeyType(slice);
    if (type == KeyType::P256 && !rules.enabled(featurePasskey))
        return std::nullopt;
    return type;
}

bool
verifyDigest(
    PublicKey const& publicKey,
    UInt256 const& digest,
    Slice const& sig,
    bool mustBeFullyCanonical) noexcept
{
    if (publicKeyType(publicKey) != KeyType::Secp256k1)
        logicError("sign: secp256k1 required for digest signing");
    auto const canonicality = ecdsaCanonicality(sig);
    if (!canonicality)
        return false;
    if (mustBeFullyCanonical && (*canonicality != ECDSACanonicality::FullyCanonical))
        return false;

    secp256k1_pubkey pubkeyImp;
    if (secp256k1_ec_pubkey_parse(
            secp256k1Context(),
            &pubkeyImp,
            reinterpret_cast<unsigned char const*>(publicKey.data()),
            publicKey.size()) != 1)
        return false;

    secp256k1_ecdsa_signature sigImp;
    if (secp256k1_ecdsa_signature_parse_der(
            secp256k1Context(),
            &sigImp,
            reinterpret_cast<unsigned char const*>(sig.data()),
            sig.size()) != 1)
        return false;
    if (*canonicality != ECDSACanonicality::FullyCanonical)
    {
        secp256k1_ecdsa_signature sigNorm;
        if (secp256k1_ecdsa_signature_normalize(secp256k1Context(), &sigNorm, &sigImp) != 1)
            return false;
        return secp256k1_ecdsa_verify(
                   secp256k1Context(),
                   &sigNorm,
                   reinterpret_cast<unsigned char const*>(digest.data()),
                   &pubkeyImp) == 1;
    }
    return secp256k1_ecdsa_verify(
               secp256k1Context(),
               &sigImp,
               reinterpret_cast<unsigned char const*>(digest.data()),
               &pubkeyImp) == 1;
}

namespace {

struct ECDSASignature
{
    std::array<std::uint8_t, 32> r{};
    std::array<std::uint8_t, 32> s{};
};

// Half the P-256 group order, (n - 1) / 2, big-endian; a low-S signature has s at most this.
constexpr std::array<std::uint8_t, 32> kP256HalfOrder{
    0x7F, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xDE, 0x73, 0x7D, 0x56, 0xD3, 0x8B, 0xCF, 0x42, 0x79, 0xDC, 0xE5, 0x61, 0x7E, 0x31, 0x92, 0xA8,
};

// Right-aligns a minimal DER INTEGER into 32 bytes; false when it is wider than 32 bytes.
bool
copyInteger(Slice integer, std::array<std::uint8_t, 32>& out) noexcept
{
    // sigPart allows a leading zero only where it keeps the high bit from reading as a sign.
    if (integer[0] == 0x00)
        integer += 1;
    if (integer.size() > out.size())
        return false;
    std::memcpy(out.data() + (out.size() - integer.size()), integer.data(), integer.size());
    return true;
}

// Parses a strict DER SEQUENCE { INTEGER r, INTEGER s }: minimal INTEGERs, a SEQUENCE
// length that covers exactly the rest, and nothing after s. A signature has one encoding.
std::optional<ECDSASignature>
parseDERSignature(Slice const& der)
{
    if (der.size() < 8 || der.size() > 72 || der[0] != 0x30 || der[1] != der.size() - 2)
        return std::nullopt;

    Slice rest = der + 2;
    auto const r = sigPart(rest);
    auto const s = sigPart(rest);
    ECDSASignature result;
    if (!r || !s || !rest.empty() || !copyInteger(*r, result.r) || !copyInteger(*s, result.s))
        return std::nullopt;
    return result;
}

// Verifies an ECDSA signature over a SHA-256 digest with the P-256 point (x, y).
bool
verifyP256ECDSA(
    Sha256Hasher::result_type const& digest,
    ECDSASignature const& sig,
    std::uint8_t const* x,
    std::uint8_t const* y) noexcept
{
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> const key{
        EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), &EC_KEY_free};
    std::unique_ptr<BIGNUM, decltype(&BN_free)> const bnX{BN_bin2bn(x, 32, nullptr), &BN_free};
    std::unique_ptr<BIGNUM, decltype(&BN_free)> const bnY{BN_bin2bn(y, 32, nullptr), &BN_free};
    std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> const ecdsaSig{
        ECDSA_SIG_new(), &ECDSA_SIG_free};
    // LCOV_EXCL_START
    if (key == nullptr || bnX == nullptr || bnY == nullptr || ecdsaSig == nullptr)
        return false;
    // LCOV_EXCL_STOP

    // Refuses coordinates that are not a point on the curve.
    if (EC_KEY_set_public_key_affine_coordinates(key.get(), bnX.get(), bnY.get()) != 1)
        return false;

    // ECDSA_SIG_set0 takes ownership of r and s only when it succeeds.
    BIGNUM* const r = BN_bin2bn(sig.r.data(), 32, nullptr);
    BIGNUM* const s = BN_bin2bn(sig.s.data(), 32, nullptr);
    // LCOV_EXCL_START
    if (ECDSA_SIG_set0(ecdsaSig.get(), r, s) != 1)
    {
        BN_free(r);
        BN_free(s);
        return false;
    }
    // LCOV_EXCL_STOP

    return ECDSA_do_verify(
               digest.data(), static_cast<int>(digest.size()), ecdsaSig.get(), key.get()) == 1;
}

}  // namespace

bool
verify(PublicKey const& publicKey, Slice const& m, Slice const& sig) noexcept
{
    if (auto const type = publicKeyType(publicKey))
    {
        if (*type == KeyType::Secp256k1)
        {
            return verifyDigest(publicKey, sha512Half(m), sig);
        }
        if (*type == KeyType::Ed25519)
        {
            if (!ed25519Canonical(sig))
                return false;

            // We internally prefix Ed25519 keys with a 0xED
            // byte to distinguish them from secp256k1 keys
            // so when verifying the signature, we need to
            // first strip that prefix.
            return ed25519_sign_open(m.data(), m.size(), publicKey.data() + 1, sig.data()) == 0;
        }
        XRPL_ASSERT(*type == KeyType::P256, "xrpl::verify : P-256 is the remaining key type");
        auto const parsedSig = parseDERSignature(sig);
        if (!parsedSig)
            return false;

        // Only the low-S form of (r, s) and (r, n - s) is accepted, so it cannot be swapped.
        if (std::ranges::lexicographical_compare(kP256HalfOrder, parsedSig->s))
            return false;

        // The key is the 0xF6 prefix followed by the 32-byte x and y coordinates.
        return verifyP256ECDSA(sha256(m), *parsedSig, publicKey.data() + 1, publicKey.data() + 33);
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
