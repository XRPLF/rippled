/** @file
 *  Implementation of XRPL public key construction, type detection,
 *  signature canonicality enforcement, signature verification, and
 *  node identity derivation.
 *
 *  Both supported elliptic curve systems — secp256k1 and Ed25519 — are
 *  handled here. The 33-byte inline buffer in `PublicKey` unifies them:
 *  secp256k1 compressed keys are natively 33 bytes (0x02/0x03 prefix),
 *  while Ed25519 keys are 32 bytes prefixed with the XRPL-specific 0xED
 *  marker. The lead byte therefore acts as a self-describing type tag.
 */
#include <xrpl/protocol/PublicKey.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/secp256k1.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <boost/multiprecision/number.hpp>

#include <ed25519.h>
#include <secp256k1.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
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

/** Decode a Base58Check-encoded public key.
 *
 *  Validates both the Base58 token-type framing and the decoded key bytes
 *  via `publicKeyType`. Safe to call on untrusted input: any malformed or
 *  unrecognized encoding returns `std::nullopt` rather than throwing.
 *
 *  @param type The expected `TokenType` prefix (e.g. `NodePublic` or
 *      `AccountPublic`).
 *  @param s The Base58Check-encoded string to decode.
 *  @return A `PublicKey` on success, or `std::nullopt` if the string is
 *      not valid Base58Check for the given type or the decoded bytes do
 *      not represent a recognized key type.
 */
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

/** Parse and consume one DER integer component from a signature buffer.
 *
 *  Expects the encoding `0x02 <length> <value>` at the start of `buf`.
 *  Advances `buf` past the consumed bytes on success. Rejects values
 *  that are negative (high bit set), zero, or carry unnecessary zero
 *  padding — all real DER malformations observed in practice.
 *
 *  @param buf Buffer positioned at the start of the integer tag; advanced
 *      in place past the consumed component on success.
 *  @return A `Slice` over the integer bytes, or `std::nullopt` if the
 *      encoding is invalid.
 */
static std::optional<Slice>
sigPart(Slice& buf)
{
    if (buf.size() < 3 || buf[0] != 0x02)
        return std::nullopt;
    auto const len = buf[1];
    buf += 2;
    if (len > buf.size() || len < 1 || len > 33)
        return std::nullopt;
    if ((buf[0] & 0x80) != 0)
        return std::nullopt;
    if (buf[0] == 0)
    {
        // A single zero byte is not a valid integer encoding.
        if (len == 1)
            return std::nullopt;
        // A leading zero is only valid when it prevents the high bit
        // from being interpreted as a sign bit.
        if ((buf[1] & 0x80) == 0)
            return std::nullopt;
    }
    std::optional<Slice> number = Slice(buf.data(), len);
    buf += len;
    return number;
}

/** Convert a raw byte slice to a non-negative hex literal for big-integer parsing.
 *
 *  Produces a `"0x..."` string suitable for constructing a
 *  `boost::multiprecision::number`. When the high bit of the first byte is
 *  set the output is prefixed with `"0x00"` to ensure the value is treated
 *  as a non-negative integer by the multiprecision parser.
 *
 *  @param slice The big-endian byte sequence to encode.
 *  @return A hex string starting with `"0x"` representing the unsigned
 *      integer value of `slice`.
 */
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
    for (int i = 0; i < slice.size(); ++i)
    {
        constexpr char kHEX[] = "0123456789ABCDEF";
        s += kHEX[((slice[i] & 0xf0) >> 4)];
        s += kHEX[((slice[i] & 0x0f) >> 0)];
    }
    return s;
}

/** Classify the canonicality of a DER-encoded secp256k1 ECDSA signature.
 *
 *  For any signed message, `(R, S)` and `(R, G-S)` are both mathematically
 *  valid ECDSA signatures. Allowing both forms enables transaction
 *  malleability: an attacker can flip S to produce a different serialization
 *  — and thus a different transaction ID — without invalidating the
 *  signature. XRPL addresses this by defining a *fully canonical* signature
 *  as one where `S <= G/2` (equivalently `S <= G-S`). Signatures where
 *  `S > G/2` are *canonical* but not fully canonical; callers may choose
 *  whether to accept them.
 *
 *  The DER structure checked is:
 *  `0x30 <total-len> 0x02 <lenR> <R> 0x02 <lenS> <S>`
 *
 *  Only the structure and canonicality of the encoding are checked; no
 *  cryptographic verification is performed.
 *
 *  @param sig DER-encoded ECDSA signature to examine.
 *  @return `ECDSACanonicality::FullyCanonical` if `S <= G-S`,
 *      `ECDSACanonicality::Canonical` if `S > G-S` but the signature is
 *      otherwise structurally valid, or `std::nullopt` if the encoding is
 *      malformed (wrong header bytes, invalid integer components, R or S
 *      outside the curve order, trailing bytes, etc.).
 *
 *  @note See https://xrpl.org/transaction-malleability.html for the
 *      broader context on malleability and the rationale for requiring
 *      fully canonical signatures.
 */
std::optional<ECDSACanonicality>
ecdsaCanonicality(Slice const& sig)
{
    using uint264 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
        264,
        264,
        boost::multiprecision::signed_magnitude,
        boost::multiprecision::unchecked,
        void>>;

    // secp256k1 curve order G.
    static uint264 const kG(
        "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");  // NOLINT(readability-identifier-naming)

    // DER structure: 0x30 <len> [ 0x02 <lenR> <R> ] [ 0x02 <lenS> <S> ]
    if ((sig.size() < 8) || (sig.size() > 72))
        return std::nullopt;
    if ((sig[0] != 0x30) || (sig[1] != (sig.size() - 2)))
        return std::nullopt;
    Slice p = sig + 2;
    auto r = sigPart(p);
    auto s = sigPart(p);
    if (!r || !s || !p.empty())
        return std::nullopt;

    uint264 const rNum(sliceToHex(*r));
    if (rNum >= kG)
        return std::nullopt;

    uint264 const sNum(sliceToHex(*s));
    if (sNum >= kG)
        return std::nullopt;

    // Both (R,S) and (R,G-S) are canonical; only S <= G-S is fully canonical.
    auto const Sp = kG - sNum;  // NOLINT(readability-identifier-naming)
    if (sNum > Sp)
        return ECDSACanonicality::Canonical;
    return ECDSACanonicality::FullyCanonical;
}

/** Check whether an Ed25519 signature's scalar S is in canonical form.
 *
 *  Per the Ed25519 spec, the second 32 bytes of a signature encode the
 *  scalar S in little-endian order. S must be strictly less than the
 *  Ed25519 subgroup order `l` to be canonical; signatures with `S >= l`
 *  are rejected to prevent malleability analogous to ECDSA.
 *
 *  @param sig The 64-byte Ed25519 signature to check.
 *  @return `true` if the signature is exactly 64 bytes and `S < l`,
 *      `false` otherwise.
 */
static bool
ed25519Canonical(Slice const& sig)
{
    if (sig.size() != 64)
        return false;
    // Big-endian representation of the Ed25519 subgroup order l.
    // NOLINTNEXTLINE(readability-identifier-naming)
    std::uint8_t const Order[] = {
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0xDE, 0xF9, 0xDE, 0xA2, 0xF7,
        0x9C, 0xD6, 0x58, 0x12, 0x63, 0x1A, 0x5C, 0xF5, 0xD3, 0xED,
    };
    // S is the second 32 bytes of the signature, stored little-endian;
    // byte-reverse to big-endian for lexicographic comparison.
    auto const le = sig.data() + 32;
    std::uint8_t S[32];  // NOLINT(readability-identifier-naming)
    std::reverse_copy(le, le + 32, S);
    return std::lexicographical_compare(S, S + 32, Order, Order + 32);
}

//------------------------------------------------------------------------------

/** Construct a `PublicKey` from a raw byte slice.
 *
 *  Copies exactly `kSIZE` (33) bytes from `slice` after validating that
 *  the bytes represent a recognised key type (secp256k1 or Ed25519).
 *  Calls `logicError` — which terminates the process — rather than
 *  throwing, because receiving an invalid key here indicates a
 *  programming error (e.g., bypassed deserialization), not a recoverable
 *  runtime condition.
 *
 *  @param slice Byte sequence to construct from; must be at least 33 bytes
 *      long and satisfy `publicKeyType(slice) != std::nullopt`.
 */
PublicKey::PublicKey(Slice const& slice)
{
    if (slice.size() < kSIZE)
    {
        logicError(
            "PublicKey::PublicKey - Input slice cannot be an undersized "
            "buffer");
    }

    if (!publicKeyType(slice))
        logicError("PublicKey::PublicKey invalid type");
    std::memcpy(buf_, slice.data(), kSIZE);
}

/** Copy-construct a `PublicKey` using a direct 33-byte `memcpy`. */
PublicKey::PublicKey(PublicKey const& other)
{
    std::memcpy(buf_, other.buf_, kSIZE);
}

/** Copy-assign a `PublicKey` using a direct 33-byte `memcpy`. */
PublicKey&
PublicKey::operator=(PublicKey const& other)
{
    if (this != &other)
    {
        std::memcpy(buf_, other.buf_, kSIZE);
    }

    return *this;
}

//------------------------------------------------------------------------------

/** Determine the key type encoded in a raw 33-byte key slice.
 *
 *  Uses the lead byte as a self-describing type tag: `0xED` signals an
 *  Ed25519 key (XRPL's prefix convention); `0x02` or `0x03` signal a
 *  secp256k1 compressed public key. Any other lead byte, or a slice that
 *  is not exactly 33 bytes, is unrecognised.
 *
 *  @param slice Raw bytes to inspect.
 *  @return The detected `KeyType`, or `std::nullopt` if the slice does
 *      not represent a known key type.
 */
std::optional<KeyType>
publicKeyType(Slice const& slice)
{
    if (slice.size() == 33)
    {
        if (slice[0] == 0xED)
            return KeyType::Ed25519;

        if (slice[0] == 0x02 || slice[0] == 0x03)
            return KeyType::Secp256k1;
    }

    return std::nullopt;
}

/** Verify a secp256k1 ECDSA signature against a pre-computed digest.
 *
 *  Validates the DER structure and canonicality of `sig` before invoking
 *  libsecp256k1. When the signature is merely `canonical` (i.e., `S > G/2`)
 *  and `mustBeFullyCanonical` is `false`, the S component is normalised to
 *  its low form via `secp256k1_ecdsa_signature_normalize` before
 *  verification. This preserves backward compatibility with old-style
 *  signatures without accepting truly invalid encodings.
 *
 *  @param publicKey The secp256k1 public key; calling with any other key
 *      type calls `logicError` (programming error).
 *  @param digest The 256-bit digest over which the signature was produced.
 *  @param sig DER-encoded ECDSA signature.
 *  @param mustBeFullyCanonical If `true`, reject signatures where
 *      `S > G/2` (the default). If `false`, accept them after normalisation.
 *  @return `true` if the signature is valid for the given key and digest,
 *      `false` for any structural, canonicality, or cryptographic failure.
 */
bool
verifyDigest(
    PublicKey const& publicKey,
    uint256 const& digest,
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

/** Verify a signature over a raw message for either supported key type.
 *
 *  Dispatches on the key type detected from `publicKey`:
 *  - **secp256k1**: hashes `m` with SHA-512 Half (yielding a 256-bit
 *    digest) and delegates to `verifyDigest` with `mustBeFullyCanonical`
 *    defaulting to `true`.
 *  - **Ed25519**: checks canonicality of `sig` first, then strips the
 *    XRPL-specific `0xED` prefix byte from `publicKey` before calling
 *    the underlying `ed25519_sign_open` library function. The prefix is
 *    an XRPL encoding artifact; the Ed25519 library is unaware of it.
 *
 *  @param publicKey The public key against which to verify.
 *  @param m The message that was signed.
 *  @param sig The signature to verify.
 *  @return `true` if the signature is valid, `false` for any failure
 *      including unrecognised key type, non-canonical signature, or
 *      cryptographic mismatch.
 */
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

            // The 0xED prefix byte is an XRPL encoding artifact that
            // distinguishes Ed25519 keys from secp256k1 keys; strip it
            // before passing the raw 32-byte key to the library.
            return ed25519_sign_open(m.data(), m.size(), publicKey.data() + 1, sig.data()) == 0;
        }
    }
    return false;
}

/** Derive the 160-bit node identity from a validator public key.
 *
 *  Applies RIPEMD-160(SHA-256(pubkey)) — the same `ripesha_hasher` used
 *  to derive XRPL account IDs — to produce the 160-bit `NodeID` used in
 *  the peer-to-peer layer for validator routing and identification.
 *
 *  @param pk The validator's public key (secp256k1 or Ed25519).
 *  @return The 160-bit `NodeID` uniquely identifying the validator on the
 *      peer-to-peer network.
 */
NodeID
calcNodeID(PublicKey const& pk)
{
    static_assert(NodeID::kBYTES == sizeof(RipeshaHasher::result_type));

    RipeshaHasher h;
    h(pk.data(), pk.size());
    return NodeID{static_cast<RipeshaHasher::result_type>(h)};
}

}  // namespace xrpl
