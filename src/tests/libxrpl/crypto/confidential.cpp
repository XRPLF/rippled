#include <xrpl/crypto/confidential.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace xrpl;
using namespace xrpl::confidential;

namespace {

Scalar
mustRandomScalar()
{
    static std::uint32_t counter = 1;
    for (std::uint32_t attempt = 0; attempt < 100000; ++attempt)
    {
        Scalar sk{};
        std::uint32_t const n = counter++;
        sk[24] = static_cast<std::uint8_t>((n >> 24) & 0xff);
        sk[25] = static_cast<std::uint8_t>((n >> 16) & 0xff);
        sk[26] = static_cast<std::uint8_t>((n >> 8) & 0xff);
        sk[27] = static_cast<std::uint8_t>(n & 0xff);
        sk[28] = 0x01;
        sk[31] = static_cast<std::uint8_t>(attempt & 0xff);
        CompressedPoint pk{};
        if (pointMulBase(sk, pk))
            return sk;
    }
    ADD_FAILURE() << "failed to find test scalar";
    return {};
}

struct Keypair
{
    Scalar sk{};
    CompressedPoint pk{};
};

Keypair
makeKey()
{
    Keypair kp;
    kp.sk = mustRandomScalar();
    EXPECT_TRUE(pointMulBase(kp.sk, kp.pk));
    return kp;
}

std::array<std::uint8_t, 20>
acct(std::uint8_t tag)
{
    std::array<std::uint8_t, 20> a{};
    a.fill(tag);
    return a;
}

std::array<std::uint8_t, 24>
issuance(std::uint8_t tag)
{
    std::array<std::uint8_t, 24> a{};
    a.fill(tag);
    return a;
}

}  // namespace

TEST(ConfidentialCrypto, RejectsMalformedScalarAndPoint)
{
    Scalar s{};
    EXPECT_FALSE(parseScalar(Slice(nullptr, 0), s));
    std::array<std::uint8_t, 31> shortS{};
    EXPECT_FALSE(parseScalar(Slice(shortS.data(), shortS.size()), s));

    std::array<std::uint8_t, 32> zero{};
    EXPECT_FALSE(parseScalar(Slice(zero.data(), zero.size()), s));

    std::array<std::uint8_t, 32> order{};
    // n itself is invalid
    unsigned char const kOrder[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
        0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
    EXPECT_FALSE(parseScalar(Slice(kOrder, 32), s));

    CompressedPoint p{};
    std::array<std::uint8_t, 33> bad{};
    bad[0] = 0x04;  // uncompressed prefix rejected
    EXPECT_FALSE(parseCompressedPoint(Slice(bad.data(), bad.size()), p));
    bad[0] = 0x02;
    // random x unlikely on curve; still may pass rarely — force all-zero x
    EXPECT_FALSE(parseCompressedPoint(Slice(bad.data(), bad.size()), p));
}

TEST(ConfidentialCrypto, ElGamalHomomorphismAndEncZero)
{
    auto alice = makeKey();
    Scalar r1 = mustRandomScalar();
    Scalar r2 = mustRandomScalar();

    Ciphertext c1{};
    Ciphertext c2{};
    ASSERT_TRUE(elgamalEncrypt(alice.pk, 7, r1, c1));
    ASSERT_TRUE(elgamalEncrypt(alice.pk, 11, r2, c2));

    Ciphertext sum{};
    ASSERT_TRUE(elgamalAdd(c1, c2, sum));

    Scalar rSum{};
    // Homomorphic check against fresh encryption of 18 with r1+r2 is awkward
    // without scalar export; check subtract cancels.
    Ciphertext back{};
    ASSERT_TRUE(elgamalSub(sum, c2, back));
    EXPECT_EQ(back.c1, c1.c1);
    EXPECT_EQ(back.c2, c1.c2);

    Ciphertext zero{};
    ASSERT_TRUE(elgamalEncrypt(alice.pk, 0, r1, zero));
    Ciphertext rerand{};
    ASSERT_TRUE(elgamalRerandomize(c1, alice.pk, r2, rerand));
    EXPECT_NE(rerand.c1, c1.c1);
    EXPECT_NE(rerand.c2, c1.c2);

    auto a = acct(0x11);
    auto iss = acct(0x22);
    auto cur = issuance(0x33);
    Ciphertext ez1{};
    Ciphertext ez2{};
    ASSERT_TRUE(encZero(
        alice.pk,
        Slice(a.data(), a.size()),
        Slice(iss.data(), iss.size()),
        Slice(cur.data(), cur.size()),
        ez1));
    ASSERT_TRUE(encZero(
        alice.pk,
        Slice(a.data(), a.size()),
        Slice(iss.data(), iss.size()),
        Slice(cur.data(), cur.size()),
        ez2));
    EXPECT_EQ(ez1.c1, ez2.c1);
    EXPECT_EQ(ez1.c2, ez2.c2);

    CiphertextBytes raw{};
    ASSERT_TRUE(serializeCiphertext(ez1, Slice(raw.data(), raw.size())));
    Ciphertext parsed{};
    ASSERT_TRUE(parseCiphertext(Slice(raw.data(), raw.size()), parsed));
    EXPECT_EQ(parsed.c1, ez1.c1);
    EXPECT_EQ(parsed.c2, ez1.c2);

    // Truncated ciphertext rejected
    EXPECT_FALSE(parseCiphertext(Slice(raw.data(), 65), parsed));
}

TEST(ConfidentialCrypto, PedersenAndContextBinding)
{
    Scalar r = mustRandomScalar();
    CompressedPoint pc1{};
    CompressedPoint pc2{};
    ASSERT_TRUE(pedersenCommit(42, r, pc1));
    ASSERT_TRUE(pedersenCommit(42, r, pc2));
    EXPECT_EQ(pc1, pc2);
    CompressedPoint pc3{};
    ASSERT_TRUE(pedersenCommit(43, r, pc3));
    EXPECT_NE(pc1, pc3);

    auto a = acct(1);
    auto id = issuance(2);
    auto recv = acct(3);
    auto ctx1 = transactionContextIDSend(
        86,
        Slice(a.data(), a.size()),
        Slice(id.data(), id.size()),
        7,
        Slice(recv.data(), recv.size()),
        1);
    auto ctx2 = transactionContextIDSend(
        86,
        Slice(a.data(), a.size()),
        Slice(id.data(), id.size()),
        7,
        Slice(recv.data(), recv.size()),
        2);
    EXPECT_NE(ctx1, ctx2);
}

TEST(ConfidentialCrypto, SchnorrRegisterRoundTrip)
{
    auto kp = makeKey();
    auto a = acct(9);
    auto id = issuance(8);
    auto ctx = transactionContextIDConvert(
        85, Slice(a.data(), a.size()), Slice(id.data(), id.size()), 1);

    SchnorrRegisterProof proof{};
    ASSERT_TRUE(proveSchnorrRegister(
        kp.sk, kp.pk, Slice(ctx.data(), ctx.size()), proof));
    EXPECT_TRUE(verifySchnorrRegister(
        kp.pk, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    // Wrong context rejected
    auto ctxBad = transactionContextIDConvert(
        85, Slice(a.data(), a.size()), Slice(id.data(), id.size()), 2);
    EXPECT_FALSE(verifySchnorrRegister(
        kp.pk,
        Slice(ctxBad.data(), ctxBad.size()),
        Slice(proof.data(), proof.size())));

    // Truncation rejected
    EXPECT_FALSE(verifySchnorrRegister(
        kp.pk, Slice(ctx.data(), ctx.size()), Slice(proof.data(), 63)));
}

TEST(ConfidentialCrypto, SendSigmaRoundTrip)
{
    auto sender = makeKey();
    auto recv = makeKey();
    auto issuer = makeKey();
    auto auditor = makeKey();

    std::uint64_t const m = 5;
    std::uint64_t const b = 20;
    Scalar r = mustRandomScalar();
    Scalar rho = mustRandomScalar();
    Scalar rb = mustRandomScalar();

    Ciphertext bal{};
    ASSERT_TRUE(elgamalEncrypt(sender.pk, b, rb, bal));

    SendSigmaPublicInput pub;
    pub.recipientKeys = {sender.pk, recv.pk, issuer.pk, auditor.pk};
    pub.senderKey = sender.pk;
    Ciphertext amountCt{};
    ASSERT_TRUE(elgamalEncrypt(sender.pk, m, r, amountCt));
    pub.c1 = amountCt.c1;
    pub.c2.clear();
    for (auto const& pk : pub.recipientKeys)
    {
        Ciphertext ct{};
        ASSERT_TRUE(elgamalEncrypt(pk, m, r, ct));
        EXPECT_EQ(ct.c1, pub.c1);
        pub.c2.push_back(ct.c2);
    }
    ASSERT_TRUE(pedersenCommit(m, r, pub.amountCommitment));
    ASSERT_TRUE(pedersenCommit(b, rho, pub.balanceCommitment));
    pub.balanceC1 = bal.c1;
    pub.balanceC2 = bal.c2;

    auto acctA = acct(0xAA);
    auto acctR = acct(0xBB);
    auto id = issuance(0xCC);
    auto ctx = transactionContextIDSend(
        86,
        Slice(acctA.data(), acctA.size()),
        Slice(id.data(), id.size()),
        10,
        Slice(acctR.data(), acctR.size()),
        3);

    SendSigmaWitness wit;
    wit.amount = amountToScalar(m);
    wit.randomness = r;
    wit.balance = amountToScalar(b);
    wit.balanceBlind = rho;
    wit.senderSk = sender.sk;

    SendSigmaProof proof{};
    ASSERT_TRUE(proveSendSigma(pub, wit, Slice(ctx.data(), ctx.size()), proof));
    EXPECT_TRUE(verifySendSigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    // Version replay / context change fails
    auto ctx2 = transactionContextIDSend(
        86,
        Slice(acctA.data(), acctA.size()),
        Slice(id.data(), id.size()),
        10,
        Slice(acctR.data(), acctR.size()),
        4);
    EXPECT_FALSE(verifySendSigma(
        pub, Slice(ctx2.data(), ctx2.size()), Slice(proof.data(), proof.size())));
}

TEST(ConfidentialCrypto, ConvertBackAndClawbackSigma)
{
    auto holder = makeKey();
    std::uint64_t const b = 100;
    Scalar rho = mustRandomScalar();
    Scalar rb = mustRandomScalar();
    Ciphertext bal{};
    ASSERT_TRUE(elgamalEncrypt(holder.pk, b, rb, bal));
    CompressedPoint pcb{};
    ASSERT_TRUE(pedersenCommit(b, rho, pcb));

    ConvertBackSigmaPublicInput pub;
    pub.holderKey = holder.pk;
    pub.balanceC1 = bal.c1;
    pub.balanceC2 = bal.c2;
    pub.balanceCommitment = pcb;

    auto a = acct(0x01);
    auto id = issuance(0x02);
    auto ctx = transactionContextIDConvertBack(
        88, Slice(a.data(), a.size()), Slice(id.data(), id.size()), 5, 9);

    ConvertBackSigmaWitness wit;
    wit.balance = amountToScalar(b);
    wit.balanceBlind = rho;
    wit.holderSk = holder.sk;

    ConvertBackSigmaProof proof{};
    ASSERT_TRUE(
        proveConvertBackSigma(pub, wit, Slice(ctx.data(), ctx.size()), proof));
    EXPECT_TRUE(verifyConvertBackSigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    auto issuer = makeKey();
    std::uint64_t const revealed = 17;
    Scalar rIss = mustRandomScalar();
    Ciphertext issBal{};
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, revealed, rIss, issBal));

    ClawbackSigmaPublicInput cpub;
    cpub.issuerKey = issuer.pk;
    cpub.issuerBalance = issBal;
    cpub.revealedAmount = revealed;

    auto holderAcct = acct(0x77);
    auto cctx = transactionContextIDClawback(
        89,
        Slice(a.data(), a.size()),
        Slice(id.data(), id.size()),
        1,
        Slice(holderAcct.data(), holderAcct.size()));

    ClawbackSigmaProof cproof{};
    ASSERT_TRUE(proveClawbackSigma(
        cpub, issuer.sk, Slice(cctx.data(), cctx.size()), cproof));
    EXPECT_TRUE(verifyClawbackSigma(
        cpub, Slice(cctx.data(), cctx.size()), Slice(cproof.data(), cproof.size())));

    cpub.revealedAmount = revealed + 1;
    EXPECT_FALSE(verifyClawbackSigma(
        cpub, Slice(cctx.data(), cctx.size()), Slice(cproof.data(), cproof.size())));
}

TEST(ConfidentialCrypto, BulletproofSingleAndAggregated)
{
    Scalar r = mustRandomScalar();
    CompressedPoint pc{};
    ASSERT_TRUE(pedersenCommit(5, r, pc));
    std::array<std::uint8_t, kSingleBulletproofBytes> p5{};
    ASSERT_TRUE(proveBulletproofSingle(pc, 5, r, p5));
    EXPECT_TRUE(verifyBulletproofSingle(pc, Slice(p5.data(), p5.size())));

    CompressedPoint pc0{};
    Scalar r0v = mustRandomScalar();
    ASSERT_TRUE(pedersenCommit(0, r0v, pc0));
    std::array<std::uint8_t, kSingleBulletproofBytes> p0{};
    ASSERT_TRUE(proveBulletproofSingle(pc0, 0, r0v, p0));
    EXPECT_TRUE(verifyBulletproofSingle(pc0, Slice(p0.data(), p0.size())));

    p5.back() ^= 0x01;
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(p5.data(), p5.size())));

    Scalar r0 = mustRandomScalar();
    Scalar r1 = mustRandomScalar();
    CompressedPoint c0{};
    CompressedPoint c1{};
    ASSERT_TRUE(pedersenCommit(5, r0, c0));
    ASSERT_TRUE(pedersenCommit(7, r1, c1));
    std::array<std::uint8_t, kAggregatedBulletproofBytes> agg{};
    ASSERT_TRUE(proveBulletproofAggregated(c0, c1, 5, 7, r0, r1, agg));
    EXPECT_TRUE(verifyBulletproofAggregated(c0, c1, Slice(agg.data(), agg.size())));
    EXPECT_FALSE(verifyBulletproofAggregated(c1, c0, Slice(agg.data(), agg.size())));

    // Aggregated proofs cache 128 generators; a later 64-bit proof must still
    // MSM against exactly 64 of them (Bünz et al. 2017/1066, n = 64).
    Scalar rAfter = mustRandomScalar();
    CompressedPoint pcAfter{};
    ASSERT_TRUE(pedersenCommit(3, rAfter, pcAfter));
    std::array<std::uint8_t, kSingleBulletproofBytes> pAfter{};
    ASSERT_TRUE(proveBulletproofSingle(pcAfter, 3, rAfter, pAfter));
    EXPECT_TRUE(
        verifyBulletproofSingle(pcAfter, Slice(pAfter.data(), pAfter.size())));
}
