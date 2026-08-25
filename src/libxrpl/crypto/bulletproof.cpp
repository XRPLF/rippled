#include <xrpl/crypto/confidential.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>

#include <openssl/bn.h>
#include <openssl/sha.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace xrpl {
namespace confidential {
namespace {

constexpr std::size_t kBits = 64;
constexpr std::string_view kTagBp = "CMPT_BULLETPROOF";

secp256k1_context const*
bpCtx()
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
                    "Unable to randomize Bulletproof secp256k1 context");
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

unsigned char const kOrderBE[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
    0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

bool
isZero(Scalar const& s)
{
    return std::all_of(s.begin(), s.end(), [](auto v) { return v == 0; });
}

uint256
sha512Half(void const* data, std::size_t n)
{
    unsigned char digest[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<unsigned char const*>(data), n, digest);
    return uint256::fromVoid(digest);
}

bool
bnToScalar(BIGNUM const* v, Scalar& out)
{
    return BN_bn2binpad(v, out.data(), 32) == 32;
}

bool
scalarAdd(Scalar const& a, Scalar const& b, Scalar& out)
{
    BN_CTX* c = BN_CTX_new();
    if (!c)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_add(R, A, B, N, c) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(c);
    return ok;
}

bool
scalarSub(Scalar const& a, Scalar const& b, Scalar& out)
{
    BN_CTX* c = BN_CTX_new();
    if (!c)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_sub(R, A, B, N, c) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(c);
    return ok;
}

bool
scalarMul(Scalar const& a, Scalar const& b, Scalar& out)
{
    BN_CTX* c = BN_CTX_new();
    if (!c)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* B = BN_bin2bn(b.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && B && N && R && BN_mod_mul(R, A, B, N, c) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(B);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(c);
    return ok;
}

bool
scalarInv(Scalar const& a, Scalar& out)
{
    if (isZero(a))
        return false;
    BN_CTX* c = BN_CTX_new();
    if (!c)
        return false;
    BIGNUM* A = BN_bin2bn(a.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = A && N && R && BN_mod_inverse(R, A, N, c) && bnToScalar(R, out);
    BN_clear_free(A);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(c);
    return ok;
}

bool
oneScalar(Scalar& out)
{
    out = {};
    out[31] = 1;
    return true;
}

bool
randomScalar(Scalar& out)
{
    auto& rng = cryptoPrng();
    for (int i = 0; i < 128; ++i)
    {
        rng(out.data(), out.size());
        if (secp256k1_ec_seckey_verify(bpCtx(), out.data()) == 1)
            return true;
    }
    return false;
}

bool
parsePk(CompressedPoint const& p, secp256k1_pubkey& out)
{
    return secp256k1_ec_pubkey_parse(bpCtx(), &out, p.data(), p.size()) == 1;
}

bool
serPk(secp256k1_pubkey const& p, CompressedPoint& out)
{
    std::size_t len = out.size();
    return secp256k1_ec_pubkey_serialize(
               bpCtx(), out.data(), &len, &p, SECP256K1_EC_COMPRESSED) == 1 &&
        len == out.size();
}

bool
mulBase(Scalar const& k, secp256k1_pubkey& out)
{
    if (isZero(k))
        return false;
    return secp256k1_ec_pubkey_create(bpCtx(), &out, k.data()) == 1;
}

bool
mulPoint(secp256k1_pubkey const& p, Scalar const& k, secp256k1_pubkey& out)
{
    if (isZero(k))
        return false;
    out = p;
    return secp256k1_ec_pubkey_tweak_mul(bpCtx(), &out, k.data()) == 1;
}

bool
addPk(secp256k1_pubkey const& a, secp256k1_pubkey const& b, secp256k1_pubkey& out)
{
    // secp256k1_ec_pubkey_combine memsets the output first; inputs must not alias it.
    secp256k1_pubkey aa = a;
    secp256k1_pubkey bb = b;
    secp256k1_pubkey const* ins[2] = {&aa, &bb};
    secp256k1_pubkey r{};
    if (secp256k1_ec_pubkey_combine(bpCtx(), &r, ins, 2) != 1)
        return false;
    out = r;
    return true;
}

bool
negPk(secp256k1_pubkey& p)
{
    return secp256k1_ec_pubkey_negate(bpCtx(), &p) == 1;
}

bool
subPk(secp256k1_pubkey const& a, secp256k1_pubkey const& b, secp256k1_pubkey& out)
{
    secp256k1_pubkey nb = b;
    if (!negPk(nb))
        return false;
    return addPk(a, nb, out);
}

/** MSM: sum k_i P_i, skipping zero scalars. Returns false if the result is infinity. */
bool
msm(
    std::vector<secp256k1_pubkey> const& pts,
    std::vector<Scalar> const& ks,
    secp256k1_pubkey& out)
{
    if (pts.size() != ks.size() || pts.empty())
        return false;
    bool have = false;
    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        if (isZero(ks[i]))
            continue;
        secp256k1_pubkey term{};
        if (!mulPoint(pts[i], ks[i], term))
            return false;
        if (!have)
        {
            out = term;
            have = true;
        }
        else if (!addPk(out, term, out))
            return false;
    }
    return have;
}

bool
numsPoint(std::string_view tag, std::uint32_t index, CompressedPoint& out)
{
    std::vector<std::uint8_t> pre(tag.begin(), tag.end());
    pre.push_back(static_cast<std::uint8_t>((index >> 24) & 0xff));
    pre.push_back(static_cast<std::uint8_t>((index >> 16) & 0xff));
    pre.push_back(static_cast<std::uint8_t>((index >> 8) & 0xff));
    pre.push_back(static_cast<std::uint8_t>(index & 0xff));
    pre.resize(pre.size() + 4);
    for (std::uint32_t seq = 0; seq < 4096; ++seq)
    {
        pre[pre.size() - 4] = static_cast<std::uint8_t>((seq >> 24) & 0xff);
        pre[pre.size() - 3] = static_cast<std::uint8_t>((seq >> 16) & 0xff);
        pre[pre.size() - 2] = static_cast<std::uint8_t>((seq >> 8) & 0xff);
        pre[pre.size() - 1] = static_cast<std::uint8_t>(seq & 0xff);
        auto const dig = sha512Half(pre.data(), pre.size());
        for (unsigned char prefix : {0x02, 0x03})
        {
            out[0] = prefix;
            std::memcpy(out.data() + 1, dig.data(), 32);
            secp256k1_pubkey pk{};
            if (secp256k1_ec_pubkey_parse(bpCtx(), &pk, out.data(), out.size()) == 1)
                return true;
        }
    }
    return false;
}

std::vector<secp256k1_pubkey>
generatorPrefix(
    std::string_view tag,
    std::size_t n,
    std::vector<secp256k1_pubkey>& cache,
    std::mutex& mutex)
{
    std::lock_guard const lock(mutex);
    auto const have = cache.size();
    if (have < n)
    {
        cache.resize(n);
        for (std::size_t i = have; i < n; ++i)
        {
            CompressedPoint p{};
            if (!numsPoint(tag, static_cast<std::uint32_t>(i), p) ||
                !parsePk(p, cache[i]))
            {
                cache.resize(have);
                return {};
            }
        }
    }
    return {cache.begin(), cache.begin() + static_cast<std::ptrdiff_t>(n)};
}

std::vector<secp256k1_pubkey>
gensG(std::size_t n)
{
    static std::vector<secp256k1_pubkey> cache;
    static std::mutex mutex;
    return generatorPrefix("CMPT_BP_G", n, cache, mutex);
}

std::vector<secp256k1_pubkey>
gensH(std::size_t n)
{
    static std::vector<secp256k1_pubkey> cache;
    static std::mutex mutex;
    return generatorPrefix("CMPT_BP_H", n, cache, mutex);
}

void
appendPoint(std::vector<std::uint8_t>& buf, CompressedPoint const& p)
{
    buf.insert(buf.end(), p.begin(), p.end());
}

void
appendScalar(std::vector<std::uint8_t>& buf, Scalar const& s)
{
    buf.insert(buf.end(), s.begin(), s.end());
}

bool
hashToScalar(std::vector<std::uint8_t> const& transcript, Scalar& out)
{
    auto const dig = sha512Half(transcript.data(), transcript.size());
    BN_CTX* c = BN_CTX_new();
    if (!c)
        return false;
    BIGNUM* X = BN_bin2bn(dig.data(), 32, nullptr);
    BIGNUM* N = BN_bin2bn(kOrderBE, 32, nullptr);
    BIGNUM* R = BN_new();
    bool ok = X && N && R && BN_mod(R, X, N, c) && bnToScalar(R, out) && !isZero(out);
    BN_clear_free(X);
    BN_clear_free(N);
    BN_clear_free(R);
    BN_CTX_free(c);
    return ok;
}

bool
vectorInner(std::vector<Scalar> const& a, std::vector<Scalar> const& b, Scalar& out)
{
    if (a.size() != b.size())
        return false;
    out = {};
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        Scalar p{};
        if (!scalarMul(a[i], b[i], p) || !scalarAdd(out, p, out))
            return false;
    }
    return true;
}

bool
powY(Scalar const& y, std::size_t n, std::vector<Scalar>& out)
{
    out.resize(n);
    if (!oneScalar(out[0]))
        return false;
    for (std::size_t i = 1; i < n; ++i)
    {
        if (!scalarMul(out[i - 1], y, out[i]))
            return false;
    }
    return true;
}

struct RangeProof
{
    CompressedPoint A{};
    CompressedPoint S{};
    CompressedPoint T1{};
    CompressedPoint T2{};
    std::vector<CompressedPoint> L;
    std::vector<CompressedPoint> R;
    Scalar tauX{};
    Scalar mu{};
    Scalar tHat{};
    Scalar a{};
    Scalar b{};
};

bool
serializeProof(RangeProof const& p, std::span<std::uint8_t> out)
{
    std::size_t const logN = p.L.size();
    std::size_t const need =
        4 * kCompressedPointBytes + 2 * logN * kCompressedPointBytes + 5 * kScalarBytes;
    if (out.size() != need)
        return false;
    std::size_t o = 0;
    auto putP = [&](CompressedPoint const& pt) {
        std::memcpy(out.data() + o, pt.data(), kCompressedPointBytes);
        o += kCompressedPointBytes;
    };
    auto putS = [&](Scalar const& s) {
        std::memcpy(out.data() + o, s.data(), kScalarBytes);
        o += kScalarBytes;
    };
    putP(p.A);
    putP(p.S);
    putP(p.T1);
    putP(p.T2);
    for (std::size_t i = 0; i < logN; ++i)
    {
        putP(p.L[i]);
        putP(p.R[i]);
    }
    putS(p.tauX);
    putS(p.mu);
    putS(p.tHat);
    putS(p.a);
    putS(p.b);
    return o == out.size();
}

bool
parseProof(Slice in, std::size_t logN, RangeProof& p)
{
    std::size_t const need =
        4 * kCompressedPointBytes + 2 * logN * kCompressedPointBytes + 5 * kScalarBytes;
    if (in.size() != need)
        return false;
    std::size_t o = 0;
    auto getP = [&](CompressedPoint& pt) -> bool {
        if (!parseCompressedPoint(Slice(in.data() + o, kCompressedPointBytes), pt))
            return false;
        o += kCompressedPointBytes;
        return true;
    };
    auto getS = [&](Scalar& s) -> bool {
        std::memcpy(s.data(), in.data() + o, kScalarBytes);
        o += kScalarBytes;
        return true;
    };
    if (!getP(p.A) || !getP(p.S) || !getP(p.T1) || !getP(p.T2))
        return false;
    p.L.resize(logN);
    p.R.resize(logN);
    for (std::size_t i = 0; i < logN; ++i)
    {
        if (!getP(p.L[i]) || !getP(p.R[i]))
            return false;
    }
    return getS(p.tauX) && getS(p.mu) && getS(p.tHat) && getS(p.a) && getS(p.b);
}

bool
commitGplusH(
    secp256k1_pubkey const& H,
    Scalar const& gCoeff,
    Scalar const& hCoeff,
    secp256k1_pubkey& out)
{
    secp256k1_pubkey gTerm{};
    secp256k1_pubkey hTerm{};
    bool have = false;
    if (!isZero(gCoeff))
    {
        if (!mulBase(gCoeff, gTerm))
            return false;
        out = gTerm;
        have = true;
    }
    if (!isZero(hCoeff))
    {
        if (!mulPoint(H, hCoeff, hTerm))
            return false;
        if (!have)
        {
            out = hTerm;
            have = true;
        }
        else if (!addPk(out, hTerm, out))
            return false;
    }
    return have;
}

bool
proveRange(
    std::vector<CompressedPoint> const& Vs,
    std::vector<std::uint64_t> const& values,
    std::vector<Scalar> const& blinds,
    RangeProof& proof)
{
    std::size_t const m = values.size();
    if (m == 0 || m != blinds.size() || m != Vs.size() || (m != 1 && m != 2))
        return false;
    std::size_t const N = kBits * m;
    std::size_t logN = 0;
    for (std::size_t t = N; t > 1; t >>= 1)
        ++logN;
    if ((1ull << logN) != N)
        return false;

    auto const Gv = gensG(N);
    auto const Hv = gensH(N);
    if (Gv.size() != N || Hv.size() != N)
        return false;

    secp256k1_pubkey H{};
    if (!parsePk(pedersenNumsGenerator(), H))
        return false;

    std::vector<Scalar> aL(N);
    std::vector<Scalar> aR(N);
    Scalar one{};
    oneScalar(one);
    for (std::size_t j = 0; j < m; ++j)
    {
        for (std::size_t i = 0; i < kBits; ++i)
        {
            std::size_t const idx = j * kBits + i;
            aL[idx] = {};
            if ((values[j] >> i) & 1ull)
                aL[idx][31] = 1;
            if (!scalarSub(aL[idx], one, aR[idx]))
                return false;
        }
    }

    Scalar alpha{};
    Scalar rho{};
    if (!randomScalar(alpha) || !randomScalar(rho))
        return false;

    std::vector<Scalar> sL(N);
    std::vector<Scalar> sR(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        if (!randomScalar(sL[i]) || !randomScalar(sR[i]))
            return false;
    }

    auto addMsmIfAny = [&](secp256k1_pubkey& acc,
                           std::vector<secp256k1_pubkey> const& pts,
                           std::vector<Scalar> const& ks) -> bool {
        bool any = false;
        for (auto const& k : ks)
        {
            if (!isZero(k))
            {
                any = true;
                break;
            }
        }
        if (!any)
            return true;
        secp256k1_pubkey tmp{};
        return msm(pts, ks, tmp) && addPk(acc, tmp, acc);
    };

    secp256k1_pubkey A{};
    if (!mulPoint(H, alpha, A))
        return false;
    if (!addMsmIfAny(A, Gv, aL) || !addMsmIfAny(A, Hv, aR))
        return false;
    if (!serPk(A, proof.A))
        return false;

    secp256k1_pubkey S{};
    if (!mulPoint(H, rho, S))
        return false;
    if (!addMsmIfAny(S, Gv, sL) || !addMsmIfAny(S, Hv, sR))
        return false;
    if (!serPk(S, proof.S))
        return false;

    std::vector<std::uint8_t> tr(kTagBp.begin(), kTagBp.end());
    tr.push_back(static_cast<std::uint8_t>(m));
    for (auto const& V : Vs)
        appendPoint(tr, V);
    appendPoint(tr, proof.A);
    appendPoint(tr, proof.S);
    Scalar y{};
    if (!hashToScalar(tr, y))
        return false;
    appendScalar(tr, y);
    Scalar z{};
    if (!hashToScalar(tr, z))
        return false;

    std::vector<Scalar> yn;
    if (!powY(y, N, yn))
        return false;

    std::vector<Scalar> twoN(kBits);
    if (!oneScalar(twoN[0]))
        return false;
    Scalar two{};
    two[31] = 2;
    for (std::size_t i = 1; i < kBits; ++i)
    {
        if (!scalarMul(twoN[i - 1], two, twoN[i]))
            return false;
    }

    std::vector<Scalar> l0(N);
    std::vector<Scalar> l1 = sL;
    std::vector<Scalar> r0(N);
    std::vector<Scalar> r1(N);
    Scalar zz{};
    if (!scalarMul(z, z, zz))
        return false;

    for (std::size_t i = 0; i < N; ++i)
    {
        if (!scalarSub(aL[i], z, l0[i]))
            return false;
        Scalar arpz{};
        if (!scalarAdd(aR[i], z, arpz) || !scalarMul(yn[i], arpz, r0[i]))
            return false;
        std::size_t const j = i / kBits;
        std::size_t const bit = i % kBits;
        Scalar zpow = zz;
        for (std::size_t k = 0; k < j; ++k)
        {
            if (!scalarMul(zpow, z, zpow))
                return false;
        }
        Scalar term{};
        if (!scalarMul(zpow, twoN[bit], term) || !scalarAdd(r0[i], term, r0[i]))
            return false;
        if (!scalarMul(yn[i], sR[i], r1[i]))
            return false;
    }

    Scalar t0{};
    Scalar t2{};
    Scalar t1a{};
    Scalar t1b{};
    if (!vectorInner(l0, r0, t0) || !vectorInner(l1, r1, t2) ||
        !vectorInner(l0, r1, t1a) || !vectorInner(l1, r0, t1b))
        return false;
    Scalar t1{};
    if (!scalarAdd(t1a, t1b, t1))
        return false;

    Scalar tau1{};
    Scalar tau2{};
    if (!randomScalar(tau1) || !randomScalar(tau2))
        return false;
    secp256k1_pubkey T1{};
    secp256k1_pubkey T2{};
    if (!commitGplusH(H, t1, tau1, T1) || !serPk(T1, proof.T1))
        return false;
    if (!commitGplusH(H, t2, tau2, T2) || !serPk(T2, proof.T2))
        return false;

    appendPoint(tr, proof.T1);
    appendPoint(tr, proof.T2);
    Scalar x{};
    if (!hashToScalar(tr, x))
        return false;

    Scalar x2{};
    if (!scalarMul(x, x, x2))
        return false;
    Scalar tauX{};
    Scalar tHat{};
    {
        Scalar a{};
        Scalar b{};
        if (!scalarMul(tau2, x2, a) || !scalarMul(tau1, x, b) || !scalarAdd(a, b, tauX))
            return false;
        Scalar zpow = zz;
        for (std::size_t j = 0; j < m; ++j)
        {
            Scalar term{};
            if (!scalarMul(zpow, blinds[j], term) || !scalarAdd(tauX, term, tauX))
                return false;
            if (!scalarMul(zpow, z, zpow))
                return false;
        }
        Scalar t1x{};
        Scalar t2x2{};
        if (!scalarMul(t1, x, t1x) || !scalarMul(t2, x2, t2x2) || !scalarAdd(t0, t1x, tHat) ||
            !scalarAdd(tHat, t2x2, tHat))
            return false;
    }

    Scalar mu{};
    {
        Scalar rhox{};
        if (!scalarMul(rho, x, rhox) || !scalarAdd(alpha, rhox, mu))
            return false;
    }

    std::vector<Scalar> l(N);
    std::vector<Scalar> r(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        Scalar sLx{};
        Scalar sRx{};
        if (!scalarMul(l1[i], x, sLx) || !scalarAdd(l0[i], sLx, l[i]))
            return false;
        if (!scalarMul(r1[i], x, sRx) || !scalarAdd(r0[i], sRx, r[i]))
            return false;
    }

    appendScalar(tr, tauX);
    appendScalar(tr, mu);
    appendScalar(tr, tHat);
    Scalar uChal{};
    if (!hashToScalar(tr, uChal))
        return false;
    secp256k1_pubkey U{};
    if (!mulBase(uChal, U))
        return false;

    std::vector<secp256k1_pubkey> Gcur = Gv;
    std::vector<secp256k1_pubkey> Hcur(N);
    Scalar yInv{};
    if (!scalarInv(y, yInv))
        return false;
    Scalar yInvPow{};
    oneScalar(yInvPow);
    for (std::size_t i = 0; i < N; ++i)
    {
        if (!mulPoint(Hv[i], yInvPow, Hcur[i]))
            return false;
        if (!scalarMul(yInvPow, yInv, yInvPow))
            return false;
    }

    auto aVec = l;
    auto bVec = r;
    proof.L.resize(logN);
    proof.R.resize(logN);
    std::size_t nCur = N;
    for (std::size_t round = 0; round < logN; ++round)
    {
        std::size_t const n2 = nCur / 2;
        std::vector<Scalar> aLo(aVec.begin(), aVec.begin() + n2);
        std::vector<Scalar> aHi(aVec.begin() + n2, aVec.begin() + nCur);
        std::vector<Scalar> bLo(bVec.begin(), bVec.begin() + n2);
        std::vector<Scalar> bHi(bVec.begin() + n2, bVec.begin() + nCur);
        std::vector<secp256k1_pubkey> gLo(Gcur.begin(), Gcur.begin() + n2);
        std::vector<secp256k1_pubkey> gHi(Gcur.begin() + n2, Gcur.begin() + nCur);
        std::vector<secp256k1_pubkey> hLo(Hcur.begin(), Hcur.begin() + n2);
        std::vector<secp256k1_pubkey> hHi(Hcur.begin() + n2, Hcur.begin() + nCur);

        Scalar cL{};
        Scalar cR{};
        if (!vectorInner(aLo, bHi, cL) || !vectorInner(aHi, bLo, cR))
            return false;

        secp256k1_pubkey L{};
        secp256k1_pubkey R{};
        secp256k1_pubkey t{};
        if (!msm(gHi, aLo, L) || !msm(hLo, bHi, t) || !addPk(L, t, L))
            return false;
        if (!mulPoint(U, cL, t) || !addPk(L, t, L) || !serPk(L, proof.L[round]))
            return false;
        if (!msm(gLo, aHi, R) || !msm(hHi, bLo, t) || !addPk(R, t, R))
            return false;
        if (!mulPoint(U, cR, t) || !addPk(R, t, R) || !serPk(R, proof.R[round]))
            return false;

        appendPoint(tr, proof.L[round]);
        appendPoint(tr, proof.R[round]);
        Scalar xi{};
        if (!hashToScalar(tr, xi))
            return false;
        Scalar xiInv{};
        if (!scalarInv(xi, xiInv))
            return false;

        std::vector<Scalar> aNew(n2);
        std::vector<Scalar> bNew(n2);
        std::vector<secp256k1_pubkey> gNew(n2);
        std::vector<secp256k1_pubkey> hNew(n2);
        for (std::size_t i = 0; i < n2; ++i)
        {
            Scalar t1s{};
            Scalar t2s{};
            if (!scalarMul(aLo[i], xi, t1s) || !scalarMul(aHi[i], xiInv, t2s) ||
                !scalarAdd(t1s, t2s, aNew[i]))
                return false;
            if (!scalarMul(bLo[i], xiInv, t1s) || !scalarMul(bHi[i], xi, t2s) ||
                !scalarAdd(t1s, t2s, bNew[i]))
                return false;
            secp256k1_pubkey glo{};
            secp256k1_pubkey ghi{};
            if (!mulPoint(gLo[i], xiInv, glo) || !mulPoint(gHi[i], xi, ghi) ||
                !addPk(glo, ghi, gNew[i]))
                return false;
            secp256k1_pubkey hlo{};
            secp256k1_pubkey hhi{};
            if (!mulPoint(hLo[i], xi, hlo) || !mulPoint(hHi[i], xiInv, hhi) ||
                !addPk(hlo, hhi, hNew[i]))
                return false;
        }
        aVec.swap(aNew);
        bVec.swap(bNew);
        Gcur.swap(gNew);
        Hcur.swap(hNew);
        nCur = n2;
    }

    proof.tauX = tauX;
    proof.mu = mu;
    proof.tHat = tHat;
    proof.a = aVec[0];
    proof.b = bVec[0];
    return true;
}

bool
verifyRange(std::vector<CompressedPoint> const& Vs, RangeProof const& proof)
{
    std::size_t const m = Vs.size();
    if (m != 1 && m != 2)
        return false;
    std::size_t const N = kBits * m;
    std::size_t logN = 0;
    for (std::size_t t = N; t > 1; t >>= 1)
        ++logN;
    if (proof.L.size() != logN || proof.R.size() != logN)
        return false;

    auto const Gv = gensG(N);
    auto const Hv = gensH(N);
    if (Gv.size() != N || Hv.size() != N)
        return false;
    secp256k1_pubkey H{};
    if (!parsePk(pedersenNumsGenerator(), H))
        return false;

    std::vector<std::uint8_t> tr(kTagBp.begin(), kTagBp.end());
    tr.push_back(static_cast<std::uint8_t>(m));
    for (auto const& V : Vs)
        appendPoint(tr, V);
    appendPoint(tr, proof.A);
    appendPoint(tr, proof.S);
    Scalar y{};
    if (!hashToScalar(tr, y))
        return false;
    appendScalar(tr, y);
    Scalar z{};
    if (!hashToScalar(tr, z))
        return false;
    appendPoint(tr, proof.T1);
    appendPoint(tr, proof.T2);
    Scalar x{};
    if (!hashToScalar(tr, x))
        return false;
    appendScalar(tr, proof.tauX);
    appendScalar(tr, proof.mu);
    appendScalar(tr, proof.tHat);
    Scalar uChal{};
    if (!hashToScalar(tr, uChal))
        return false;

    std::vector<Scalar> xs(logN);
    for (std::size_t i = 0; i < logN; ++i)
    {
        appendPoint(tr, proof.L[i]);
        appendPoint(tr, proof.R[i]);
        if (!hashToScalar(tr, xs[i]))
            return false;
    }

    std::vector<Scalar> yn;
    if (!powY(y, N, yn))
        return false;
    std::vector<Scalar> twoN(kBits);
    oneScalar(twoN[0]);
    Scalar two{};
    two[31] = 2;
    for (std::size_t i = 1; i < kBits; ++i)
    {
        if (!scalarMul(twoN[i - 1], two, twoN[i]))
            return false;
    }

    // delta(y,z) as in Bulletproofs §4.2
    Scalar sumY{};
    for (auto const& yi : yn)
    {
        if (!scalarAdd(sumY, yi, sumY))
            return false;
    }
    Scalar sum2{};
    for (auto const& t : twoN)
    {
        if (!scalarAdd(sum2, t, sum2))
            return false;
    }
    Scalar zz{};
    if (!scalarMul(z, z, zz))
        return false;
    Scalar zMinusZz{};
    if (!scalarSub(z, zz, zMinusZz))
        return false;
    Scalar delta{};
    if (!scalarMul(zMinusZz, sumY, delta))
        return false;
    Scalar zpow = zz;
    if (!scalarMul(zpow, z, zpow))  // z^3
        return false;
    for (std::size_t j = 0; j < m; ++j)
    {
        Scalar term{};
        if (!scalarMul(zpow, sum2, term) || !scalarSub(delta, term, delta))
            return false;
        if (!scalarMul(zpow, z, zpow))
            return false;
    }

    secp256k1_pubkey lhs{};
    secp256k1_pubkey rhs{};
    secp256k1_pubkey tmp{};
    Scalar x2{};
    if (!scalarMul(x, x, x2))
        return false;
    {
        secp256k1_pubkey T1{};
        secp256k1_pubkey T2{};
        if (!parsePk(proof.T1, T1) || !parsePk(proof.T2, T2))
            return false;
        if (!mulPoint(T1, x, lhs))
            return false;
        if (!mulPoint(T2, x2, tmp) || !addPk(lhs, tmp, lhs))
            return false;
        zpow = zz;
        for (std::size_t j = 0; j < m; ++j)
        {
            secp256k1_pubkey V{};
            if (!parsePk(Vs[j], V) || !mulPoint(V, zpow, tmp) || !addPk(lhs, tmp, lhs))
                return false;
            if (!scalarMul(zpow, z, zpow))
                return false;
        }
        if (!isZero(delta))
        {
            if (!mulBase(delta, tmp) || !addPk(lhs, tmp, lhs))
                return false;
        }
        if (!commitGplusH(H, proof.tHat, proof.tauX, rhs))
            return false;
        CompressedPoint lC{};
        CompressedPoint rC{};
        if (!serPk(lhs, lC) || !serPk(rhs, rC) || lC != rC)
            return false;
    }

    std::vector<Scalar> xInv(logN);
    for (std::size_t j = 0; j < logN; ++j)
    {
        if (!scalarInv(xs[j], xInv[j]))
            return false;
    }
    Scalar yInv{};
    if (!scalarInv(y, yInv))
        return false;
    std::vector<Scalar> yInvPow(N);
    if (!oneScalar(yInvPow[0]))
        return false;
    for (std::size_t i = 1; i < N; ++i)
    {
        if (!scalarMul(yInvPow[i - 1], yInv, yInvPow[i]))
            return false;
    }

    std::vector<Scalar> sG(N);
    std::vector<Scalar> sH(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        if (!oneScalar(sG[i]) || !oneScalar(sH[i]))
            return false;
        for (std::size_t j = 0; j < logN; ++j)
        {
            bool const bit = ((i >> (logN - 1 - j)) & 1u) != 0;
            if (!scalarMul(sG[i], bit ? xs[j] : xInv[j], sG[i]))
                return false;
            if (!scalarMul(sH[i], bit ? xInv[j] : xs[j], sH[i]))
                return false;
        }
        if (!scalarMul(sH[i], yInvPow[i], sH[i]))
            return false;
    }

    Scalar zero{};
    Scalar negZ{};
    if (!scalarSub(zero, z, negZ))
        return false;
    std::vector<Scalar> gCoeff(N);
    std::vector<Scalar> hCoeff(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        gCoeff[i] = negZ;
        std::size_t const j = i / kBits;
        std::size_t const bit = i % kBits;
        Scalar zpow2 = zz;
        for (std::size_t k = 0; k < j; ++k)
        {
            if (!scalarMul(zpow2, z, zpow2))
                return false;
        }
        Scalar twoTerm{};
        if (!scalarMul(zpow2, twoN[bit], twoTerm) ||
            !scalarMul(twoTerm, yInvPow[i], twoTerm) ||
            !scalarAdd(z, twoTerm, hCoeff[i]))
            return false;
    }

    secp256k1_pubkey P{};
    secp256k1_pubkey A{};
    secp256k1_pubkey Sp{};
    if (!parsePk(proof.A, A) || !parsePk(proof.S, Sp) || !mulPoint(Sp, x, tmp) ||
        !addPk(A, tmp, P))
        return false;
    if (!msm(Gv, gCoeff, tmp) || !addPk(P, tmp, P))
        return false;
    if (!msm(Hv, hCoeff, tmp) || !addPk(P, tmp, P))
        return false;
    secp256k1_pubkey U{};
    if (!mulBase(uChal, U) || !mulPoint(U, proof.tHat, tmp) || !addPk(P, tmp, P))
        return false;
    if (!mulPoint(H, proof.mu, tmp) || !subPk(P, tmp, P))
        return false;
    for (std::size_t j = 0; j < logN; ++j)
    {
        Scalar xj2{};
        Scalar xjInv2{};
        if (!scalarMul(xs[j], xs[j], xj2) || !scalarInv(xj2, xjInv2))
            return false;
        secp256k1_pubkey Lj{};
        secp256k1_pubkey Rj{};
        if (!parsePk(proof.L[j], Lj) || !parsePk(proof.R[j], Rj))
            return false;
        if (!mulPoint(Lj, xj2, tmp) || !addPk(P, tmp, P))
            return false;
        if (!mulPoint(Rj, xjInv2, tmp) || !addPk(P, tmp, P))
            return false;
    }

    Scalar ab{};
    if (!scalarMul(proof.a, proof.b, ab))
        return false;
    std::vector<Scalar> cG(N);
    std::vector<Scalar> cH(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        if (!scalarMul(proof.a, sG[i], cG[i]) || !scalarMul(proof.b, sH[i], cH[i]))
            return false;
    }
    secp256k1_pubkey rec{};
    if (!msm(Gv, cG, rec))
        return false;
    secp256k1_pubkey recH{};
    if (!msm(Hv, cH, recH) || !addPk(rec, recH, rec))
        return false;
    if (!mulPoint(U, ab, tmp) || !addPk(rec, tmp, rec))
        return false;
    CompressedPoint recC{};
    CompressedPoint pC{};
    return serPk(rec, recC) && serPk(P, pC) && recC == pC;
}

}  // namespace

bool
proveBulletproofSingle(
    CompressedPoint const& commitment,
    std::uint64_t value,
    Scalar const& blinding,
    std::array<std::uint8_t, kSingleBulletproofBytes>& out) noexcept
{
    RangeProof proof;
    if (!proveRange({commitment}, {value}, {blinding}, proof))
        return false;
    return serializeProof(proof, out);
}

bool
verifyBulletproofSingle(CompressedPoint const& commitment, Slice proof) noexcept
{
    RangeProof p;
    if (!parseProof(proof, 6, p))
        return false;
    return verifyRange({commitment}, p);
}

bool
proveBulletproofAggregated(
    CompressedPoint const& commitment0,
    CompressedPoint const& commitment1,
    std::uint64_t value0,
    std::uint64_t value1,
    Scalar const& blinding0,
    Scalar const& blinding1,
    std::array<std::uint8_t, kAggregatedBulletproofBytes>& out) noexcept
{
    RangeProof proof;
    if (!proveRange(
            {commitment0, commitment1},
            {value0, value1},
            {blinding0, blinding1},
            proof))
        return false;
    return serializeProof(proof, out);
}

bool
verifyBulletproofAggregated(
    CompressedPoint const& commitment0,
    CompressedPoint const& commitment1,
    Slice proof) noexcept
{
    RangeProof p;
    if (!parseProof(proof, 7, p))
        return false;
    return verifyRange({commitment0, commitment1}, p);
}

bool
proveBulletproofSend(
    CompressedPoint const& amountCommitment,
    CompressedPoint const& remainingCommitment,
    std::uint64_t amount,
    std::uint64_t remaining,
    Scalar const& amountBlinding,
    Scalar const& remainingBlinding,
    std::array<std::uint8_t, kAggregatedBulletproofBytes>& out) noexcept
{
    if (amount == 0)
        return false;

    CompressedPoint oneG{};
    CompressedPoint positiveAmountCommitment{};
    if (!pointMulBase(amountToScalar(1), oneG) ||
        !pointSub(amountCommitment, oneG, positiveAmountCommitment))
        return false;

    return proveBulletproofAggregated(
        positiveAmountCommitment,
        remainingCommitment,
        amount - 1,
        remaining,
        amountBlinding,
        remainingBlinding,
        out);
}

bool
verifyBulletproofSend(
    CompressedPoint const& amountCommitment,
    CompressedPoint const& remainingCommitment,
    Slice proof) noexcept
{
    CompressedPoint oneG{};
    CompressedPoint positiveAmountCommitment{};
    if (!pointMulBase(amountToScalar(1), oneG) ||
        !pointSub(amountCommitment, oneG, positiveAmountCommitment))
        return false;
    return verifyBulletproofAggregated(
        positiveAmountCommitment, remainingCommitment, proof);
}

}  // namespace confidential
}  // namespace xrpl
