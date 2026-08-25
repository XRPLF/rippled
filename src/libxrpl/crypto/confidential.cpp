#include <xrpl/crypto/confidential.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>

#include <openssl/bn.h>
#include <openssl/sha.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace xrpl {
namespace confidential {
namespace {

secp256k1_context const*
ctx()
{
    struct Holder
    {
        secp256k1_context* impl;
        Holder()
            : impl(secp256k1_context_create(
                  SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN))
        {
            std::array<unsigned char, 32> seed{};
            cryptoPrng()(seed.data(), seed.size());
            if (!impl || secp256k1_context_randomize(impl, seed.data()) != 1)
            {
                secureErase(seed.data(), seed.size());
                if (impl)
                    secp256k1_context_destroy(impl);
                throw std::runtime_error(
                    "Unable to randomize confidential secp256k1 context");
            }
            secureErase(seed.data(), seed.size());
        }
        ~Holder()
        {
            secp256k1_context_destroy(impl);
        }
    };
    static Holder const h;
    return h.impl;
}

// secp256k1 group order n (big-endian).
unsigned char const kOrderBE[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
    0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

uint256
sha512HalfRaw(void const* data, std::size_t size) noexcept
{
    unsigned char digest[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<unsigned char const*>(data), size, digest);
    return uint256::fromVoid(digest);
}

uint256
sha512HalfRaw(Slice s) noexcept
{
    return sha512HalfRaw(s.data(), s.size());
}

void
appendBytes(std::vector<std::uint8_t>& buf, void const* p, std::size_t n)
{
    auto const* b = reinterpret_cast<std::uint8_t const*>(p);
    buf.insert(buf.end(), b, b + n);
}

void
appendSlice(std::vector<std::uint8_t>& buf, Slice s)
{
    appendBytes(buf, s.data(), s.size());
}

void
appendU16(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<std::uint8_t>(v & 0xff));
}

void
appendU32(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<std::uint8_t>(v & 0xff));
}

bool
scalarReduce(Slice digest32, Scalar& out) noexcept
{
    if (digest32.size() != kScalarBytes)
        return false;

    BN_CTX* bnCtx = BN_CTX_new();
    if (bnCtx == nullptr)
        return false;

    BIGNUM* x = BN_new();
    BIGNUM* n = BN_new();
    BIGNUM* r = BN_new();
    bool ok = false;

    if (x && n && r &&
        BN_bin2bn(digest32.data(), 32, x) &&
        BN_bin2bn(kOrderBE, 32, n) && BN_mod(r, x, n, bnCtx) &&
        BN_bn2binpad(r, out.data(), 32) == 32)
    {
        ok = true;
    }

    BN_clear_free(x);
    BN_clear_free(n);
    BN_clear_free(r);
    BN_CTX_free(bnCtx);
    return ok;
}

/** Hash preimage to a non-zero scalar in [1, n-1].
 * First attempt is SHA512Half(preimage) mod n (EncZero / FS base form).
 * On 0, append a 32-bit counter and retry (spec omits the zero case).
 */
bool
hashToScalar(Slice preimage, Scalar& out) noexcept
{
    auto tryDigest = [&](void const* data, std::size_t n) -> bool {
        auto const dig = sha512HalfRaw(data, n);
        if (!scalarReduce(Slice(dig.data(), dig.size()), out))
            return false;
        return secp256k1_ec_seckey_verify(ctx(), out.data()) == 1;
    };

    if (tryDigest(preimage.data(), preimage.size()))
        return true;

    std::vector<std::uint8_t> buf(
        reinterpret_cast<std::uint8_t const*>(preimage.data()),
        reinterpret_cast<std::uint8_t const*>(preimage.data()) + preimage.size());
    buf.resize(buf.size() + 4);

    for (std::uint32_t seq = 0; seq != 128; ++seq)
    {
        buf[buf.size() - 4] = static_cast<std::uint8_t>((seq >> 24) & 0xff);
        buf[buf.size() - 3] = static_cast<std::uint8_t>((seq >> 16) & 0xff);
        buf[buf.size() - 2] = static_cast<std::uint8_t>((seq >> 8) & 0xff);
        buf[buf.size() - 1] = static_cast<std::uint8_t>(seq & 0xff);
        if (tryDigest(buf.data(), buf.size()))
            return true;
    }
    return false;
}

/** Fiat–Shamir challenge: SHA512Half then reduce mod n; reject zero. */
bool
challengeFromTranscript(Slice transcript, Scalar& e) noexcept
{
    auto const dig = sha512HalfRaw(transcript);
    if (!scalarReduce(Slice(dig.data(), dig.size()), e))
        return false;
    // Compact proofs store e as a 32-byte field element; zero is degenerate.
    return secp256k1_ec_seckey_verify(ctx(), e.data()) == 1;
}

bool
parsePubkey(CompressedPoint const& p, secp256k1_pubkey& out) noexcept
{
    return secp256k1_ec_pubkey_parse(ctx(), &out, p.data(), p.size()) == 1;
}

bool
serializePubkey(secp256k1_pubkey const& p, CompressedPoint& out) noexcept
{
    std::size_t len = out.size();
    return secp256k1_ec_pubkey_serialize(
               ctx(), out.data(), &len, &p, SECP256K1_EC_COMPRESSED) == 1 &&
        len == out.size();
}

bool
randomScalar(Scalar& out) noexcept
{
    auto& rng = cryptoPrng();
    for (int i = 0; i < 128; ++i)
    {
        rng(out.data(), out.size());
        if (secp256k1_ec_seckey_verify(ctx(), out.data()) == 1)
            return true;
    }
    return false;
}

bool
bnToScalar(BIGNUM const* v, Scalar& out) noexcept
{
    if (BN_bn2binpad(v, out.data(), 32) != 32)
        return false;
    return true;
}

bool
scalarAdd(Scalar const& a, Scalar const& b, Scalar& out) noexcept
{
    BN_CTX* bnCtx = BN_CTX_new();
    if (!bnCtx)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_add(R, A, B, N, bnCtx) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(bnCtx);
    return ok;
}

bool
scalarSub(Scalar const& a, Scalar const& b, Scalar& out) noexcept
{
    BN_CTX* bnCtx = BN_CTX_new();
    if (!bnCtx)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_sub(R, A, B, N, bnCtx) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(bnCtx);
    return ok;
}

bool
scalarMul(Scalar const& a, Scalar const& b, Scalar& out) noexcept
{
    BN_CTX* bnCtx = BN_CTX_new();
    if (!bnCtx)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_mul(R, A, B, N, bnCtx) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(bnCtx);
    return ok;
}

/** out = a + e*w (mod n) for sigma responses (0 allowed). */
bool
scalarMad(Scalar const& a, Scalar const& e, Scalar const& w, Scalar& out) noexcept
{
    Scalar ew{};
    if (!scalarMul(e, w, ew))
        return false;
    return scalarAdd(a, ew, out);
}

bool
pointMulBaseImpl(Scalar const& k, secp256k1_pubkey& out) noexcept
{
    return secp256k1_ec_pubkey_create(ctx(), &out, k.data()) == 1;
}

bool
pointMulImpl(
    secp256k1_pubkey const& p,
    Scalar const& k,
    secp256k1_pubkey& out) noexcept
{
    out = p;
    return secp256k1_ec_pubkey_tweak_mul(ctx(), &out, k.data()) == 1;
}

bool
pointNegateImpl(secp256k1_pubkey& p) noexcept
{
    return secp256k1_ec_pubkey_negate(ctx(), &p) == 1;
}

bool
pointCombineImpl(
    secp256k1_pubkey const* const* ins,
    std::size_t n,
    secp256k1_pubkey& out) noexcept
{
    std::vector<secp256k1_pubkey> copies(n);
    std::vector<secp256k1_pubkey const*> ptrs(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        copies[i] = *ins[i];
        ptrs[i] = &copies[i];
    }
    secp256k1_pubkey r{};
    if (secp256k1_ec_pubkey_combine(ctx(), &r, ptrs.data(), n) != 1)
        return false;
    out = r;
    return true;
}

bool
pointAddImpl(
    secp256k1_pubkey const& a,
    secp256k1_pubkey const& b,
    secp256k1_pubkey& out) noexcept
{
    secp256k1_pubkey const* ins[2] = {&a, &b};
    return pointCombineImpl(ins, 2, out);
}

bool
pointSubImpl(
    secp256k1_pubkey const& a,
    secp256k1_pubkey const& b,
    secp256k1_pubkey& out) noexcept
{
    secp256k1_pubkey neg = b;
    if (!pointNegateImpl(neg))
        return false;
    return pointAddImpl(a, neg, out);
}

/** R = aG + bP (a may be zero → omit aG). */
bool
linComb(
    Scalar const& a,
    Scalar const& b,
    secp256k1_pubkey const& p,
    secp256k1_pubkey& out) noexcept
{
    secp256k1_pubkey bP{};
    if (!pointMulImpl(p, b, bP))
        return false;

    bool const aZero = std::all_of(a.begin(), a.end(), [](auto v) {
        return v == 0;
    });
    if (aZero)
    {
        out = bP;
        return true;
    }

    secp256k1_pubkey aG{};
    if (!pointMulBaseImpl(a, aG))
        return false;
    return pointAddImpl(aG, bP, out);
}

bool
isZeroScalar(Scalar const& s) noexcept
{
    return std::all_of(s.begin(), s.end(), [](auto v) { return v == 0; });
}

/** Accept any 32-byte representative in [0, n-1]. */
bool
parseFieldElement(Slice in, Scalar& out) noexcept
{
    if (in.size() != kScalarBytes)
        return false;
    std::memcpy(out.data(), in.data(), kScalarBytes);
    if (isZeroScalar(out))
        return true;
    return secp256k1_ec_seckey_verify(ctx(), out.data()) == 1;
}

bool
scalarFromAmount(std::uint64_t amount, Scalar& out) noexcept
{
    out = {};
    for (int i = 0; i < 8; ++i)
        out[24 + i] = static_cast<std::uint8_t>((amount >> (56 - 8 * i)) & 0xff);
    return true;
}

bool
mGPlusRP(
    std::uint64_t amount,
    Scalar const& r,
    secp256k1_pubkey const& pk,
    secp256k1_pubkey& out) noexcept
{
    Scalar m{};
    scalarFromAmount(amount, m);
    return linComb(m, r, pk, out);
}

}  // namespace

bool
parseScalar(Slice in, Scalar& out) noexcept
{
    if (in.size() != kScalarBytes)
        return false;
    std::memcpy(out.data(), in.data(), kScalarBytes);
    return secp256k1_ec_seckey_verify(ctx(), out.data()) == 1;
}

bool
serializeScalar(Scalar const& in, Slice out) noexcept
{
    if (out.size() != kScalarBytes)
        return false;
    std::memcpy(const_cast<std::uint8_t*>(out.data()), in.data(), kScalarBytes);
    return true;
}

Scalar
amountToScalar(std::uint64_t amount) noexcept
{
    Scalar s{};
    scalarFromAmount(amount, s);
    return s;
}

bool
parseCompressedPoint(Slice in, CompressedPoint& out) noexcept
{
    if (in.size() != kCompressedPointBytes)
        return false;
    if (in[0] != 0x02 && in[0] != 0x03)
        return false;
    secp256k1_pubkey pk{};
    if (secp256k1_ec_pubkey_parse(ctx(), &pk, in.data(), in.size()) != 1)
        return false;
    std::memcpy(out.data(), in.data(), kCompressedPointBytes);
    return true;
}

bool
serializeCompressedPoint(CompressedPoint const& in, Slice out) noexcept
{
    if (out.size() != kCompressedPointBytes)
        return false;
    secp256k1_pubkey pk{};
    if (!parsePubkey(in, pk))
        return false;
    CompressedPoint tmp{};
    if (!serializePubkey(pk, tmp))
        return false;
    std::memcpy(const_cast<std::uint8_t*>(out.data()), tmp.data(), tmp.size());
    return true;
}

bool
pointMulBase(Scalar const& k, CompressedPoint& out) noexcept
{
    secp256k1_pubkey pk{};
    if (!pointMulBaseImpl(k, pk))
        return false;
    return serializePubkey(pk, out);
}

bool
pointAdd(
    CompressedPoint const& p,
    CompressedPoint const& q,
    CompressedPoint& out) noexcept
{
    secp256k1_pubkey a{};
    secp256k1_pubkey b{};
    secp256k1_pubkey r{};
    if (!parsePubkey(p, a) || !parsePubkey(q, b) || !pointAddImpl(a, b, r))
        return false;
    return serializePubkey(r, out);
}

bool
pointSub(
    CompressedPoint const& p,
    CompressedPoint const& q,
    CompressedPoint& out) noexcept
{
    secp256k1_pubkey a{};
    secp256k1_pubkey b{};
    secp256k1_pubkey r{};
    if (!parsePubkey(p, a) || !parsePubkey(q, b) || !pointSubImpl(a, b, r))
        return false;
    return serializePubkey(r, out);
}

bool
pointMul(
    CompressedPoint const& p,
    Scalar const& k,
    CompressedPoint& out) noexcept
{
    secp256k1_pubkey a{};
    secp256k1_pubkey r{};
    if (!parsePubkey(p, a) || !pointMulImpl(a, k, r))
        return false;
    return serializePubkey(r, out);
}

bool
pointAddMulBase(
    CompressedPoint const& p,
    Scalar const& k,
    CompressedPoint& out) noexcept
{
    secp256k1_pubkey a{};
    if (!parsePubkey(p, a))
        return false;
    if (secp256k1_ec_pubkey_tweak_add(ctx(), &a, k.data()) != 1)
        return false;
    return serializePubkey(a, out);
}

bool
parseCiphertext(Slice in, Ciphertext& out) noexcept
{
    if (in.size() != kCiphertextBytes)
        return false;
    return parseCompressedPoint(in.substr(0, 33), out.c1) &&
        parseCompressedPoint(in.substr(33, 33), out.c2);
}

bool
serializeCiphertext(Ciphertext const& in, Slice out) noexcept
{
    if (out.size() != kCiphertextBytes)
        return false;
    auto* d = const_cast<std::uint8_t*>(out.data());
    std::memcpy(d, in.c1.data(), 33);
    std::memcpy(d + 33, in.c2.data(), 33);
    // Re-parse to ensure canonical on-curve encoding.
    Ciphertext tmp{};
    return parseCiphertext(out, tmp);
}

bool
elgamalEncrypt(
    CompressedPoint const& pk,
    std::uint64_t amount,
    Scalar const& r,
    Ciphertext& out) noexcept
{
    if (secp256k1_ec_seckey_verify(ctx(), r.data()) != 1)
        return false;

    secp256k1_pubkey P{};
    secp256k1_pubkey C1{};
    secp256k1_pubkey C2{};
    if (!parsePubkey(pk, P))
        return false;
    if (!pointMulBaseImpl(r, C1))
        return false;
    if (!mGPlusRP(amount, r, P, C2))
        return false;
    return serializePubkey(C1, out.c1) && serializePubkey(C2, out.c2);
}

bool
elgamalAdd(Ciphertext const& a, Ciphertext const& b, Ciphertext& out) noexcept
{
    return pointAdd(a.c1, b.c1, out.c1) && pointAdd(a.c2, b.c2, out.c2);
}

bool
elgamalSub(
    Ciphertext const& a,
    Ciphertext const& b,
    CompressedPoint const& pk,
    Scalar const& r,
    Ciphertext& out) noexcept
{
    Ciphertext rerandomized{};
    return elgamalRerandomize(a, pk, r, rerandomized) &&
        pointSub(rerandomized.c1, b.c1, out.c1) &&
        pointSub(rerandomized.c2, b.c2, out.c2);
}

bool
elgamalRerandomize(
    Ciphertext const& in,
    CompressedPoint const& pk,
    Scalar const& r,
    Ciphertext& out) noexcept
{
    Ciphertext zero{};
    if (!elgamalEncrypt(pk, 0, r, zero))
        return false;
    return elgamalAdd(in, zero, out);
}

bool
encZero(
    CompressedPoint const& pk,
    Slice account,
    Slice issuer,
    Slice currency,
    Ciphertext& out) noexcept
{
    std::vector<std::uint8_t> pre;
    appendSlice(pre, Slice(kTagEncZero.data(), kTagEncZero.size()));
    appendSlice(pre, account);
    appendSlice(pre, issuer);
    appendSlice(pre, currency);

    Scalar r{};
    if (!hashToScalar(Slice(pre.data(), pre.size()), r))
        return false;
    return elgamalEncrypt(pk, 0, r, out);
}

CompressedPoint const&
pedersenNumsGenerator() noexcept
{
    static CompressedPoint const H = [] {
        // PROVISIONAL NUMS: try-and-increment x-coordinate decompression.
        std::vector<std::uint8_t> pre;
        static constexpr std::string_view kTag = "CMPT_NUMS_H";
        appendSlice(pre, Slice(kTag.data(), kTag.size()));
        pre.resize(pre.size() + 4);

        CompressedPoint pt{};
        for (std::uint32_t seq = 0; seq < 1024; ++seq)
        {
            pre[pre.size() - 4] = static_cast<std::uint8_t>((seq >> 24) & 0xff);
            pre[pre.size() - 3] = static_cast<std::uint8_t>((seq >> 16) & 0xff);
            pre[pre.size() - 2] = static_cast<std::uint8_t>((seq >> 8) & 0xff);
            pre[pre.size() - 1] = static_cast<std::uint8_t>(seq & 0xff);
            auto const dig = sha512HalfRaw(pre.data(), pre.size());

            for (unsigned char prefix : {0x02, 0x03})
            {
                pt[0] = prefix;
                std::memcpy(pt.data() + 1, dig.data(), 32);
                secp256k1_pubkey pk{};
                if (secp256k1_ec_pubkey_parse(ctx(), &pk, pt.data(), pt.size()) ==
                    1)
                {
                    return pt;
                }
            }
        }
        // Should be unreachable for secp256k1.
        pt = {};
        return pt;
    }();
    return H;
}

bool
pedersenCommitScalar(
    Scalar const& amount,
    Scalar const& blinding,
    CompressedPoint& out) noexcept
{
    if (secp256k1_ec_seckey_verify(ctx(), blinding.data()) != 1)
        return false;

    secp256k1_pubkey H{};
    if (!parsePubkey(pedersenNumsGenerator(), H))
        return false;

    secp256k1_pubkey rH{};
    if (!pointMulImpl(H, blinding, rH))
        return false;

    if (isZeroScalar(amount))
        return serializePubkey(rH, out);

    if (secp256k1_ec_seckey_verify(ctx(), amount.data()) != 1)
        return false;

    secp256k1_pubkey mG{};
    if (!pointMulBaseImpl(amount, mG))
        return false;
    secp256k1_pubkey pc{};
    if (!pointAddImpl(mG, rH, pc))
        return false;
    return serializePubkey(pc, out);
}

bool
pedersenCommit(
    std::uint64_t amount,
    Scalar const& blinding,
    CompressedPoint& out) noexcept
{
    return pedersenCommitScalar(amountToScalar(amount), blinding, out);
}

uint256
transactionContextID(Slice preimage) noexcept
{
    return sha512HalfRaw(preimage);
}

namespace {

uint256
contextHash(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice txSpecific) noexcept
{
    std::vector<std::uint8_t> pre;
    appendU16(pre, txType);
    appendSlice(pre, account);
    appendSlice(pre, issuanceId);
    appendU32(pre, sequenceOrTicket);
    appendSlice(pre, txSpecific);
    return sha512HalfRaw(Slice(pre.data(), pre.size()));
}

}  // namespace

uint256
transactionContextIDSend(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice receiver,
    std::uint32_t spendingVersion) noexcept
{
    std::vector<std::uint8_t> specific;
    appendSlice(specific, receiver);
    appendU32(specific, spendingVersion);
    return contextHash(
        txType,
        account,
        issuanceId,
        sequenceOrTicket,
        Slice(specific.data(), specific.size()));
}

uint256
transactionContextIDConvertBack(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    std::uint32_t spendingVersion) noexcept
{
    // eprint: Receiver := Account for ConvertBack uniformity.
    return transactionContextIDSend(
        txType, account, issuanceId, sequenceOrTicket, account, spendingVersion);
}

uint256
transactionContextIDConvert(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket) noexcept
{
    // TxSpecific := Account || 0
    std::vector<std::uint8_t> specific;
    appendSlice(specific, account);
    appendU32(specific, 0);
    return contextHash(
        txType,
        account,
        issuanceId,
        sequenceOrTicket,
        Slice(specific.data(), specific.size()));
}

uint256
transactionContextIDClawback(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice holder) noexcept
{
    // TxSpecific := Holder || 0
    std::vector<std::uint8_t> specific;
    appendSlice(specific, holder);
    appendU32(specific, 0);
    return contextHash(
        txType,
        account,
        issuanceId,
        sequenceOrTicket,
        Slice(specific.data(), specific.size()));
}

uint256
transactionContextIDMigrateAuditor(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice holder,
    std::uint32_t targetAuditorVersion) noexcept
{
    // TxSpecific := Holder || TargetAuditorVersion
    std::vector<std::uint8_t> specific;
    appendSlice(specific, holder);
    appendU32(specific, targetAuditorVersion);
    return contextHash(
        txType,
        account,
        issuanceId,
        sequenceOrTicket,
        Slice(specific.data(), specific.size()));
}

bool
proveSchnorrRegister(
    Scalar const& sk,
    CompressedPoint const& pk,
    Slice contextId,
    SchnorrRegisterProof& out) noexcept
{
    if (secp256k1_ec_seckey_verify(ctx(), sk.data()) != 1)
        return false;

    CompressedPoint expect{};
    if (!pointMulBase(sk, expect) || expect != pk)
        return false;

    Scalar k{};
    if (!randomScalar(k))
        return false;

    CompressedPoint T{};
    if (!pointMulBase(k, T))
        return false;

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript, Slice(kTagSchnorrRegister.data(), kTagSchnorrRegister.size()));
    appendSlice(transcript, Slice(pk.data(), pk.size()));
    appendSlice(transcript, Slice(T.data(), T.size()));
    appendSlice(transcript, contextId);

    Scalar e{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e))
        return false;

    Scalar s{};
    if (!scalarMad(k, e, sk, s))
        return false;

    std::memcpy(out.data(), e.data(), 32);
    std::memcpy(out.data() + 32, s.data(), 32);
    secureErase(k.data(), k.size());
    return true;
}

bool
verifySchnorrRegister(
    CompressedPoint const& pk,
    Slice contextId,
    Slice proof) noexcept
{
    if (proof.size() != kSchnorrRegisterProofBytes)
        return false;

    Scalar e{};
    Scalar s{};
    if (!parseScalar(proof.substr(0, 32), e) ||
        !parseScalar(proof.substr(32, 32), s))
        return false;

    secp256k1_pubkey P{};
    secp256k1_pubkey sG{};
    secp256k1_pubkey eP{};
    secp256k1_pubkey T{};
    if (!parsePubkey(pk, P) || !pointMulBaseImpl(s, sG) ||
        !pointMulImpl(P, e, eP) || !pointSubImpl(sG, eP, T))
        return false;

    CompressedPoint Tbytes{};
    if (!serializePubkey(T, Tbytes))
        return false;

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript, Slice(kTagSchnorrRegister.data(), kTagSchnorrRegister.size()));
    appendSlice(transcript, Slice(pk.data(), pk.size()));
    appendSlice(transcript, Slice(Tbytes.data(), Tbytes.size()));
    appendSlice(transcript, contextId);

    Scalar e2{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e2))
        return false;
    return e == e2;
}

namespace {

bool
appendPoint(std::vector<std::uint8_t>& t, CompressedPoint const& p)
{
    appendSlice(t, Slice(p.data(), p.size()));
    return true;
}

bool
reconstructSendCommitments(
    SendSigmaPublicInput const& pub,
    Scalar const& e,
    Scalar const& zm,
    Scalar const& zr,
    Scalar const& zb,
    Scalar const& zrho,
    Scalar const& zsk,
    CompressedPoint& T1,
    std::vector<CompressedPoint>& T2,
    CompressedPoint& Tm,
    CompressedPoint& Tb,
    CompressedPoint& Tsk1,
    CompressedPoint& Tsk2) noexcept
{
    auto const n = pub.recipientKeys.size();
    if (n == 0 || n != pub.c2.size())
        return false;

    secp256k1_pubkey C1{};
    secp256k1_pubkey PCm{};
    secp256k1_pubkey PCb{};
    secp256k1_pubkey B1{};
    secp256k1_pubkey B2{};
    secp256k1_pubkey PA{};
    secp256k1_pubkey H{};
    if (!parsePubkey(pub.c1, C1) || !parsePubkey(pub.amountCommitment, PCm) ||
        !parsePubkey(pub.balanceCommitment, PCb) ||
        !parsePubkey(pub.balanceC1, B1) || !parsePubkey(pub.balanceC2, B2) ||
        !parsePubkey(pub.senderKey, PA) ||
        !parsePubkey(pedersenNumsGenerator(), H))
        return false;

    // T1 = zr·G - e·C1
    secp256k1_pubkey zrG{};
    secp256k1_pubkey eC1{};
    secp256k1_pubkey t1{};
    if (!pointMulBaseImpl(zr, zrG) || !pointMulImpl(C1, e, eC1) ||
        !pointSubImpl(zrG, eC1, t1) || !serializePubkey(t1, T1))
        return false;

    T2.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        secp256k1_pubkey Pi{};
        secp256k1_pubkey C2i{};
        secp256k1_pubkey left{};
        secp256k1_pubkey eC2{};
        secp256k1_pubkey ti{};
        if (!parsePubkey(pub.recipientKeys[i], Pi) ||
            !parsePubkey(pub.c2[i], C2i) || !linComb(zm, zr, Pi, left) ||
            !pointMulImpl(C2i, e, eC2) || !pointSubImpl(left, eC2, ti) ||
            !serializePubkey(ti, T2[i]))
            return false;
    }

    // Tm = zm·G + zr·H - e·PCm
    secp256k1_pubkey leftM{};
    secp256k1_pubkey ePCm{};
    secp256k1_pubkey tm{};
    if (!linComb(zm, zr, H, leftM) || !pointMulImpl(PCm, e, ePCm) ||
        !pointSubImpl(leftM, ePCm, tm) || !serializePubkey(tm, Tm))
        return false;

    // Tb = zb·G + zρ·H - e·PCb
    secp256k1_pubkey leftB{};
    secp256k1_pubkey ePCb{};
    secp256k1_pubkey tb{};
    if (!linComb(zb, zrho, H, leftB) || !pointMulImpl(PCb, e, ePCb) ||
        !pointSubImpl(leftB, ePCb, tb) || !serializePubkey(tb, Tb))
        return false;

    // Tsk1 = zsk·G - e·PA
    secp256k1_pubkey zskG{};
    secp256k1_pubkey ePA{};
    secp256k1_pubkey tsk1{};
    if (!pointMulBaseImpl(zsk, zskG) || !pointMulImpl(PA, e, ePA) ||
        !pointSubImpl(zskG, ePA, tsk1) || !serializePubkey(tsk1, Tsk1))
        return false;

    // Tsk2 = zb·G + zsk·B1 - e·B2
    secp256k1_pubkey leftSk{};
    secp256k1_pubkey eB2{};
    secp256k1_pubkey tsk2{};
    if (!linComb(zb, zsk, B1, leftSk) || !pointMulImpl(B2, e, eB2) ||
        !pointSubImpl(leftSk, eB2, tsk2) || !serializePubkey(tsk2, Tsk2))
        return false;

    return true;
}

void
buildSendTranscript(
    SendSigmaPublicInput const& pub,
    CompressedPoint const& T1,
    std::vector<CompressedPoint> const& T2,
    CompressedPoint const& Tm,
    CompressedPoint const& Tb,
    CompressedPoint const& Tsk1,
    CompressedPoint const& Tsk2,
    Slice contextId,
    std::vector<std::uint8_t>& transcript)
{
    appendSlice(transcript, Slice(kTagSendSigma.data(), kTagSendSigma.size()));
    for (auto const& p : pub.recipientKeys)
        appendPoint(transcript, p);
    appendPoint(transcript, pub.senderKey);
    appendPoint(transcript, pub.c1);
    for (auto const& c : pub.c2)
        appendPoint(transcript, c);
    appendPoint(transcript, pub.amountCommitment);
    appendPoint(transcript, pub.balanceCommitment);
    appendPoint(transcript, pub.balanceC1);
    appendPoint(transcript, pub.balanceC2);
    appendPoint(transcript, T1);
    for (auto const& t : T2)
        appendPoint(transcript, t);
    appendPoint(transcript, Tm);
    appendPoint(transcript, Tb);
    appendPoint(transcript, Tsk1);
    appendPoint(transcript, Tsk2);
    appendSlice(transcript, contextId);
}

}  // namespace

bool
proveSendSigma(
    SendSigmaPublicInput const& pub,
    SendSigmaWitness const& wit,
    Slice contextId,
    SendSigmaProof& out) noexcept
{
    auto const n = pub.recipientKeys.size();
    if (n == 0 || n != pub.c2.size())
        return false;
    if (secp256k1_ec_seckey_verify(ctx(), wit.randomness.data()) != 1 ||
        secp256k1_ec_seckey_verify(ctx(), wit.balanceBlind.data()) != 1 ||
        secp256k1_ec_seckey_verify(ctx(), wit.senderSk.data()) != 1)
        return false;

    Scalar am{};
    Scalar ab{};
    Scalar arb{};
    Scalar ask{};
    Scalar ar{};
    if (!randomScalar(am) || !randomScalar(ar) || !randomScalar(ab) ||
        !randomScalar(arb) || !randomScalar(ask))
        return false;

    secp256k1_pubkey H{};
    if (!parsePubkey(pedersenNumsGenerator(), H))
        return false;

    CompressedPoint T1{};
    if (!pointMulBase(ar, T1))
        return false;

    std::vector<CompressedPoint> T2(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        secp256k1_pubkey Pi{};
        secp256k1_pubkey ti{};
        if (!parsePubkey(pub.recipientKeys[i], Pi) || !linComb(am, ar, Pi, ti) ||
            !serializePubkey(ti, T2[i]))
            return false;
    }

    CompressedPoint Tm{};
    CompressedPoint Tb{};
    {
        secp256k1_pubkey tm{};
        secp256k1_pubkey tb{};
        if (!linComb(am, ar, H, tm) || !serializePubkey(tm, Tm))
            return false;
        if (!linComb(ab, arb, H, tb) || !serializePubkey(tb, Tb))
            return false;
    }

    CompressedPoint Tsk1{};
    if (!pointMulBase(ask, Tsk1))
        return false;

    CompressedPoint Tsk2{};
    {
        secp256k1_pubkey B1{};
        secp256k1_pubkey tsk2{};
        if (!parsePubkey(pub.balanceC1, B1) || !linComb(ab, ask, B1, tsk2) ||
            !serializePubkey(tsk2, Tsk2))
            return false;
    }

    std::vector<std::uint8_t> transcript;
    buildSendTranscript(
        pub, T1, T2, Tm, Tb, Tsk1, Tsk2, contextId, transcript);

    Scalar e{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e))
        return false;

    Scalar zm{};
    Scalar zr{};
    Scalar zb{};
    Scalar zrho{};
    Scalar zsk{};
    if (!scalarMad(am, e, wit.amount, zm) || !scalarMad(ar, e, wit.randomness, zr) ||
        !scalarMad(ab, e, wit.balance, zb) ||
        !scalarMad(arb, e, wit.balanceBlind, zrho) ||
        !scalarMad(ask, e, wit.senderSk, zsk))
        return false;

    std::memcpy(out.data() + 0, e.data(), 32);
    std::memcpy(out.data() + 32, zm.data(), 32);
    std::memcpy(out.data() + 64, zr.data(), 32);
    std::memcpy(out.data() + 96, zb.data(), 32);
    std::memcpy(out.data() + 128, zrho.data(), 32);
    std::memcpy(out.data() + 160, zsk.data(), 32);

    secureErase(am.data(), am.size());
    secureErase(ar.data(), ar.size());
    secureErase(ab.data(), ab.size());
    secureErase(arb.data(), arb.size());
    secureErase(ask.data(), ask.size());
    return true;
}

bool
verifySendSigma(
    SendSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept
{
    if (proof.size() != kSendSigmaProofBytes)
        return false;

    Scalar e{};
    Scalar zm{};
    Scalar zr{};
    Scalar zb{};
    Scalar zrho{};
    Scalar zsk{};
    // Responses may be any field element; require canonical [1,n-1] for e and
    // non-zero blinding-related responses. Amount/balance responses may be 0.
    if (!parseScalar(proof.substr(0, 32), e) ||
        !parseFieldElement(proof.substr(32, 32), zm) ||
        !parseScalar(proof.substr(64, 32), zr) ||
        !parseFieldElement(proof.substr(96, 32), zb) ||
        !parseScalar(proof.substr(128, 32), zrho) ||
        !parseScalar(proof.substr(160, 32), zsk))
        return false;

    CompressedPoint T1{};
    std::vector<CompressedPoint> T2;
    CompressedPoint Tm{};
    CompressedPoint Tb{};
    CompressedPoint Tsk1{};
    CompressedPoint Tsk2{};
    if (!reconstructSendCommitments(
            pub, e, zm, zr, zb, zrho, zsk, T1, T2, Tm, Tb, Tsk1, Tsk2))
        return false;

    std::vector<std::uint8_t> transcript;
    buildSendTranscript(
        pub, T1, T2, Tm, Tb, Tsk1, Tsk2, contextId, transcript);

    Scalar e2{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e2))
        return false;
    return e == e2;
}

bool
proveConvertBackSigma(
    ConvertBackSigmaPublicInput const& pub,
    ConvertBackSigmaWitness const& wit,
    Slice contextId,
    ConvertBackSigmaProof& out) noexcept
{
    if (secp256k1_ec_seckey_verify(ctx(), wit.balanceBlind.data()) != 1 ||
        secp256k1_ec_seckey_verify(ctx(), wit.holderSk.data()) != 1)
        return false;

    Scalar tb{};
    Scalar tsk{};
    Scalar trho{};
    if (!randomScalar(tb) || !randomScalar(tsk) || !randomScalar(trho))
        return false;

    secp256k1_pubkey H{};
    secp256k1_pubkey B1{};
    if (!parsePubkey(pedersenNumsGenerator(), H) ||
        !parsePubkey(pub.balanceC1, B1))
        return false;

    CompressedPoint Tsk1{};
    if (!pointMulBase(tsk, Tsk1))
        return false;

    CompressedPoint Tsk2{};
    {
        secp256k1_pubkey t{};
        if (!linComb(tb, tsk, B1, t) || !serializePubkey(t, Tsk2))
            return false;
    }

    CompressedPoint Tb{};
    {
        secp256k1_pubkey t{};
        if (!linComb(tb, trho, H, t) || !serializePubkey(t, Tb))
            return false;
    }

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript,
        Slice(kTagConvertBackSigma.data(), kTagConvertBackSigma.size()));
    appendPoint(transcript, pub.holderKey);
    appendPoint(transcript, pub.balanceC1);
    appendPoint(transcript, pub.balanceC2);
    appendPoint(transcript, pub.balanceCommitment);
    appendPoint(transcript, Tsk1);
    appendPoint(transcript, Tsk2);
    appendPoint(transcript, Tb);
    appendSlice(transcript, contextId);

    Scalar e{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e))
        return false;

    Scalar zb{};
    Scalar zrho{};
    Scalar zsk{};
    if (!scalarMad(tb, e, wit.balance, zb) ||
        !scalarMad(trho, e, wit.balanceBlind, zrho) ||
        !scalarMad(tsk, e, wit.holderSk, zsk))
        return false;

    // Order: (e, z_b, z_ρ, z_sk)
    std::memcpy(out.data() + 0, e.data(), 32);
    std::memcpy(out.data() + 32, zb.data(), 32);
    std::memcpy(out.data() + 64, zrho.data(), 32);
    std::memcpy(out.data() + 96, zsk.data(), 32);

    secureErase(tb.data(), tb.size());
    secureErase(tsk.data(), tsk.size());
    secureErase(trho.data(), trho.size());
    return true;
}

bool
verifyConvertBackSigma(
    ConvertBackSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept
{
    if (proof.size() != kConvertBackSigmaProofBytes)
        return false;

    Scalar e{};
    Scalar zb{};
    Scalar zrho{};
    Scalar zsk{};
    if (!parseScalar(proof.substr(0, 32), e) ||
        !parseFieldElement(proof.substr(32, 32), zb) ||
        !parseScalar(proof.substr(64, 32), zrho) ||
        !parseScalar(proof.substr(96, 32), zsk))
        return false;

    secp256k1_pubkey PA{};
    secp256k1_pubkey B1{};
    secp256k1_pubkey B2{};
    secp256k1_pubkey PCb{};
    secp256k1_pubkey H{};
    if (!parsePubkey(pub.holderKey, PA) || !parsePubkey(pub.balanceC1, B1) ||
        !parsePubkey(pub.balanceC2, B2) ||
        !parsePubkey(pub.balanceCommitment, PCb) ||
        !parsePubkey(pedersenNumsGenerator(), H))
        return false;

    // Tsk1 = zsk·G - e·PA
    CompressedPoint Tsk1{};
    {
        secp256k1_pubkey zskG{};
        secp256k1_pubkey ePA{};
        secp256k1_pubkey t{};
        if (!pointMulBaseImpl(zsk, zskG) || !pointMulImpl(PA, e, ePA) ||
            !pointSubImpl(zskG, ePA, t) || !serializePubkey(t, Tsk1))
            return false;
    }

    // Tsk2 = zb·G + zsk·B1 - e·B2
    CompressedPoint Tsk2{};
    {
        secp256k1_pubkey left{};
        secp256k1_pubkey eB2{};
        secp256k1_pubkey t{};
        if (!linComb(zb, zsk, B1, left) || !pointMulImpl(B2, e, eB2) ||
            !pointSubImpl(left, eB2, t) || !serializePubkey(t, Tsk2))
            return false;
    }

    // Tb = zb·G + zρ·H - e·PCb
    CompressedPoint Tb{};
    {
        secp256k1_pubkey left{};
        secp256k1_pubkey ePC{};
        secp256k1_pubkey t{};
        if (!linComb(zb, zrho, H, left) || !pointMulImpl(PCb, e, ePC) ||
            !pointSubImpl(left, ePC, t) || !serializePubkey(t, Tb))
            return false;
    }

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript,
        Slice(kTagConvertBackSigma.data(), kTagConvertBackSigma.size()));
    appendPoint(transcript, pub.holderKey);
    appendPoint(transcript, pub.balanceC1);
    appendPoint(transcript, pub.balanceC2);
    appendPoint(transcript, pub.balanceCommitment);
    appendPoint(transcript, Tsk1);
    appendPoint(transcript, Tsk2);
    appendPoint(transcript, Tb);
    appendSlice(transcript, contextId);

    Scalar e2{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e2))
        return false;
    return e == e2;
}

bool
proveClawbackSigma(
    ClawbackSigmaPublicInput const& pub,
    Scalar const& issuerSk,
    Slice contextId,
    ClawbackSigmaProof& out) noexcept
{
    if (secp256k1_ec_seckey_verify(ctx(), issuerSk.data()) != 1)
        return false;

    CompressedPoint expect{};
    if (!pointMulBase(issuerSk, expect) || expect != pub.issuerKey)
        return false;

    Scalar t{};
    if (!randomScalar(t))
        return false;

    // T1 = t·G, T2 = t·C1
    CompressedPoint T1{};
    CompressedPoint T2{};
    if (!pointMulBase(t, T1))
        return false;
    if (!pointMul(pub.issuerBalance.c1, t, T2))
        return false;

    CompressedPoint mG{};
    Scalar m = amountToScalar(pub.revealedAmount);
    if (isZeroScalar(m))
    {
        // Identity has no compressed encoding; use r=0 special: hash a fixed
        // placeholder. SPEC OMISSION: encoding of 0·G in clawback transcript.
        // Use the NUMS generator as a non-consensus placeholder is wrong —
        // instead require amount > 0 for clawback proofs.
        return false;
    }
    if (!pointMulBase(m, mG))
        return false;

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript, Slice(kTagClawbackSigma.data(), kTagClawbackSigma.size()));
    appendPoint(transcript, pub.issuerKey);
    appendPoint(transcript, pub.issuerBalance.c1);
    appendPoint(transcript, pub.issuerBalance.c2);
    appendPoint(transcript, mG);
    appendPoint(transcript, T1);
    appendPoint(transcript, T2);
    appendSlice(transcript, contextId);

    Scalar e{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e))
        return false;

    Scalar z{};
    if (!scalarMad(t, e, issuerSk, z))
        return false;

    std::memcpy(out.data(), e.data(), 32);
    std::memcpy(out.data() + 32, z.data(), 32);
    secureErase(t.data(), t.size());
    return true;
}

bool
verifyClawbackSigma(
    ClawbackSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept
{
    if (proof.size() != kClawbackSigmaProofBytes)
        return false;

    Scalar e{};
    Scalar z{};
    if (!parseScalar(proof.substr(0, 32), e) ||
        !parseScalar(proof.substr(32, 32), z))
        return false;

    if (pub.revealedAmount == 0)
        return false;

    CompressedPoint mG{};
    if (!pointMulBase(amountToScalar(pub.revealedAmount), mG))
        return false;

    secp256k1_pubkey P{};
    secp256k1_pubkey C1{};
    secp256k1_pubkey C2{};
    secp256k1_pubkey MG{};
    if (!parsePubkey(pub.issuerKey, P) ||
        !parsePubkey(pub.issuerBalance.c1, C1) ||
        !parsePubkey(pub.issuerBalance.c2, C2) || !parsePubkey(mG, MG))
        return false;

    // T1 = z·G - e·P
    CompressedPoint T1{};
    {
        secp256k1_pubkey zG{};
        secp256k1_pubkey eP{};
        secp256k1_pubkey t{};
        if (!pointMulBaseImpl(z, zG) || !pointMulImpl(P, e, eP) ||
            !pointSubImpl(zG, eP, t) || !serializePubkey(t, T1))
            return false;
    }

    // T2 = z·C1 - e·(C2 - mG)
    CompressedPoint T2{};
    {
        secp256k1_pubkey C2mG{};
        secp256k1_pubkey zC1{};
        secp256k1_pubkey eRight{};
        secp256k1_pubkey t{};
        if (!pointSubImpl(C2, MG, C2mG) || !pointMulImpl(C1, z, zC1) ||
            !pointMulImpl(C2mG, e, eRight) || !pointSubImpl(zC1, eRight, t) ||
            !serializePubkey(t, T2))
            return false;
    }

    std::vector<std::uint8_t> transcript;
    appendSlice(
        transcript, Slice(kTagClawbackSigma.data(), kTagClawbackSigma.size()));
    appendPoint(transcript, pub.issuerKey);
    appendPoint(transcript, pub.issuerBalance.c1);
    appendPoint(transcript, pub.issuerBalance.c2);
    appendPoint(transcript, mG);
    appendPoint(transcript, T1);
    appendPoint(transcript, T2);
    appendSlice(transcript, contextId);

    Scalar e2{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e2))
        return false;
    return e == e2;
}

namespace {

void
buildAuditorEqualityTranscript(
    AuditorEqualitySigmaPublicInput const& pub,
    CompressedPoint const& Tx,
    CompressedPoint const& T1A,
    CompressedPoint const& T2I,
    CompressedPoint const& T2A,
    Slice contextId,
    std::vector<std::uint8_t>& transcript)
{
    appendSlice(
        transcript,
        Slice(kTagAuditorEqualitySigma.data(), kTagAuditorEqualitySigma.size()));
    appendPoint(transcript, pub.issuerKey);
    appendPoint(transcript, pub.issuerCiphertext.c1);
    appendPoint(transcript, pub.issuerCiphertext.c2);
    appendPoint(transcript, pub.auditorKey);
    appendPoint(transcript, pub.auditorCiphertext.c1);
    appendPoint(transcript, pub.auditorCiphertext.c2);
    appendPoint(transcript, Tx);
    appendPoint(transcript, T1A);
    appendPoint(transcript, T2I);
    appendPoint(transcript, T2A);
    appendSlice(transcript, contextId);
}

bool
reconstructAuditorEqualityCommitments(
    AuditorEqualitySigmaPublicInput const& pub,
    Scalar const& e,
    Scalar const& zx,
    Scalar const& zr,
    Scalar const& zm,
    CompressedPoint& Tx,
    CompressedPoint& T1A,
    CompressedPoint& T2I,
    CompressedPoint& T2A) noexcept
{
    secp256k1_pubkey PI{};
    secp256k1_pubkey C1I{};
    secp256k1_pubkey C2I{};
    secp256k1_pubkey PA{};
    secp256k1_pubkey C1A{};
    secp256k1_pubkey C2A{};
    if (!parsePubkey(pub.issuerKey, PI) ||
        !parsePubkey(pub.issuerCiphertext.c1, C1I) ||
        !parsePubkey(pub.issuerCiphertext.c2, C2I) ||
        !parsePubkey(pub.auditorKey, PA) ||
        !parsePubkey(pub.auditorCiphertext.c1, C1A) ||
        !parsePubkey(pub.auditorCiphertext.c2, C2A))
        return false;

    // Tx = zx·G - e·pk_I
    {
        secp256k1_pubkey zxG{};
        secp256k1_pubkey ePI{};
        secp256k1_pubkey t{};
        if (!pointMulBaseImpl(zx, zxG) || !pointMulImpl(PI, e, ePI) ||
            !pointSubImpl(zxG, ePI, t) || !serializePubkey(t, Tx))
            return false;
    }

    // T1A = zr·G - e·C1_A
    {
        secp256k1_pubkey zrG{};
        secp256k1_pubkey eC1A{};
        secp256k1_pubkey t{};
        if (!pointMulBaseImpl(zr, zrG) || !pointMulImpl(C1A, e, eC1A) ||
            !pointSubImpl(zrG, eC1A, t) || !serializePubkey(t, T1A))
            return false;
    }

    // T2I = zm·G + zx·C1_I - e·C2_I
    {
        secp256k1_pubkey left{};
        secp256k1_pubkey eC2I{};
        secp256k1_pubkey t{};
        if (!linComb(zm, zx, C1I, left) || !pointMulImpl(C2I, e, eC2I) ||
            !pointSubImpl(left, eC2I, t) || !serializePubkey(t, T2I))
            return false;
    }

    // T2A = zm·G + zr·pk_A - e·C2_A
    {
        secp256k1_pubkey left{};
        secp256k1_pubkey eC2A{};
        secp256k1_pubkey t{};
        if (!linComb(zm, zr, PA, left) || !pointMulImpl(C2A, e, eC2A) ||
            !pointSubImpl(left, eC2A, t) || !serializePubkey(t, T2A))
            return false;
    }

    return true;
}

bool
auditorEqualityWitnessMatches(
    AuditorEqualitySigmaPublicInput const& pub,
    AuditorEqualitySigmaWitness const& wit) noexcept
{
    CompressedPoint expectPk{};
    if (!pointMulBase(wit.issuerSk, expectPk) || expectPk != pub.issuerKey)
        return false;

    CompressedPoint expectC1A{};
    if (!pointMulBase(wit.randomness, expectC1A) ||
        expectC1A != pub.auditorCiphertext.c1)
        return false;

    secp256k1_pubkey C1I{};
    secp256k1_pubkey PA{};
    secp256k1_pubkey expectC2I{};
    secp256k1_pubkey expectC2A{};
    if (!parsePubkey(pub.issuerCiphertext.c1, C1I) ||
        !parsePubkey(pub.auditorKey, PA) ||
        !linComb(wit.amount, wit.issuerSk, C1I, expectC2I) ||
        !linComb(wit.amount, wit.randomness, PA, expectC2A))
        return false;

    CompressedPoint c2I{};
    CompressedPoint c2A{};
    if (!serializePubkey(expectC2I, c2I) || !serializePubkey(expectC2A, c2A))
        return false;
    return c2I == pub.issuerCiphertext.c2 && c2A == pub.auditorCiphertext.c2;
}

}  // namespace

bool
proveAuditorEqualitySigma(
    AuditorEqualitySigmaPublicInput const& pub,
    AuditorEqualitySigmaWitness const& wit,
    Slice contextId,
    AuditorEqualitySigmaProof& out) noexcept
{
    // x and r must be in [1, n-1]; m may be zero (empty confidential balance).
    if (secp256k1_ec_seckey_verify(ctx(), wit.issuerSk.data()) != 1 ||
        secp256k1_ec_seckey_verify(ctx(), wit.randomness.data()) != 1)
        return false;
    if (!isZeroScalar(wit.amount) &&
        secp256k1_ec_seckey_verify(ctx(), wit.amount.data()) != 1)
        return false;
    if (!auditorEqualityWitnessMatches(pub, wit))
        return false;

    Scalar ax{};
    Scalar ar{};
    Scalar am{};
    if (!randomScalar(ax) || !randomScalar(ar) || !randomScalar(am))
        return false;

    CompressedPoint Tx{};
    if (!pointMulBase(ax, Tx))
        return false;

    CompressedPoint T1A{};
    if (!pointMulBase(ar, T1A))
        return false;

    CompressedPoint T2I{};
    {
        secp256k1_pubkey C1I{};
        secp256k1_pubkey t{};
        if (!parsePubkey(pub.issuerCiphertext.c1, C1I) ||
            !linComb(am, ax, C1I, t) || !serializePubkey(t, T2I))
            return false;
    }

    CompressedPoint T2A{};
    {
        secp256k1_pubkey PA{};
        secp256k1_pubkey t{};
        if (!parsePubkey(pub.auditorKey, PA) || !linComb(am, ar, PA, t) ||
            !serializePubkey(t, T2A))
            return false;
    }

    std::vector<std::uint8_t> transcript;
    buildAuditorEqualityTranscript(pub, Tx, T1A, T2I, T2A, contextId, transcript);

    Scalar e{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e))
        return false;

    Scalar zx{};
    Scalar zr{};
    Scalar zm{};
    if (!scalarMad(ax, e, wit.issuerSk, zx) ||
        !scalarMad(ar, e, wit.randomness, zr) ||
        !scalarMad(am, e, wit.amount, zm))
        return false;

    // Order: (e, z_x, z_r, z_m)
    std::memcpy(out.data() + 0, e.data(), 32);
    std::memcpy(out.data() + 32, zx.data(), 32);
    std::memcpy(out.data() + 64, zr.data(), 32);
    std::memcpy(out.data() + 96, zm.data(), 32);

    secureErase(ax.data(), ax.size());
    secureErase(ar.data(), ar.size());
    secureErase(am.data(), am.size());
    return true;
}

bool
verifyAuditorEqualitySigma(
    AuditorEqualitySigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept
{
    if (proof.size() != kAuditorEqualitySigmaProofBytes)
        return false;

    Scalar e{};
    Scalar zx{};
    Scalar zr{};
    Scalar zm{};
    // All compact fields are scalars in [1, n-1].
    if (!parseScalar(proof.substr(0, 32), e) ||
        !parseScalar(proof.substr(32, 32), zx) ||
        !parseScalar(proof.substr(64, 32), zr) ||
        !parseScalar(proof.substr(96, 32), zm))
        return false;

    CompressedPoint Tx{};
    CompressedPoint T1A{};
    CompressedPoint T2I{};
    CompressedPoint T2A{};
    if (!reconstructAuditorEqualityCommitments(
            pub, e, zx, zr, zm, Tx, T1A, T2I, T2A))
        return false;

    std::vector<std::uint8_t> transcript;
    buildAuditorEqualityTranscript(pub, Tx, T1A, T2I, T2A, contextId, transcript);

    Scalar e2{};
    if (!challengeFromTranscript(Slice(transcript.data(), transcript.size()), e2))
        return false;
    return e == e2;
}

bool
extractSigmaChallenge(Slice proof, Scalar& e) noexcept
{
    if (proof.size() < kScalarBytes)
        return false;
    return parseScalar(Slice(proof.data(), kScalarBytes), e);
}

bool
addScalars(Scalar const& a, Scalar const& b, Scalar& out) noexcept
{
    return scalarAdd(a, b, out);
}

bool
subScalars(Scalar const& a, Scalar const& b, Scalar& out) noexcept
{
    return scalarSub(a, b, out);
}

}  // namespace confidential
}  // namespace xrpl
