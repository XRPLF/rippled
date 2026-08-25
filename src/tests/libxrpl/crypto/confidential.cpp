#include <xrpl/crypto/confidential.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
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

    Ciphertext expectedBack{};
    ASSERT_TRUE(elgamalRerandomize(c1, alice.pk, r2, expectedBack));
    Ciphertext back{};
    ASSERT_TRUE(elgamalSub(sum, c2, alice.pk, r2, back));
    EXPECT_EQ(back.c1, expectedBack.c1);
    EXPECT_EQ(back.c2, expectedBack.c2);

    Ciphertext fullDebit{};
    Ciphertext expectedZero{};
    ASSERT_TRUE(elgamalSub(c1, c1, alice.pk, r2, fullDebit));
    ASSERT_TRUE(elgamalEncrypt(alice.pk, 0, r2, expectedZero));
    EXPECT_EQ(fullDebit.c1, expectedZero.c1);
    EXPECT_EQ(fullDebit.c2, expectedZero.c2);

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

TEST(ConfidentialCrypto, BulletproofGeneratorCacheConcurrentGrowth)
{
    Scalar singleBlind = mustRandomScalar();
    Scalar aggregateBlind0 = mustRandomScalar();
    Scalar aggregateBlind1 = mustRandomScalar();
    CompressedPoint singleCommitment{};
    CompressedPoint aggregateCommitment0{};
    CompressedPoint aggregateCommitment1{};
    ASSERT_TRUE(pedersenCommit(3, singleBlind, singleCommitment));
    ASSERT_TRUE(pedersenCommit(5, aggregateBlind0, aggregateCommitment0));
    ASSERT_TRUE(pedersenCommit(7, aggregateBlind1, aggregateCommitment1));

    std::array<std::uint8_t, kSingleBulletproofBytes> singleProof{};
    std::array<std::uint8_t, kAggregatedBulletproofBytes> aggregateProof{};
    bool singleOk = false;
    bool aggregateOk = false;
    std::thread single([&] {
        singleOk =
            proveBulletproofSingle(singleCommitment, 3, singleBlind, singleProof);
    });
    std::thread aggregate([&] {
        aggregateOk = proveBulletproofAggregated(
            aggregateCommitment0,
            aggregateCommitment1,
            5,
            7,
            aggregateBlind0,
            aggregateBlind1,
            aggregateProof);
    });
    single.join();
    aggregate.join();

    EXPECT_TRUE(singleOk);
    EXPECT_TRUE(aggregateOk);
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

    std::array<std::uint8_t, kAggregatedBulletproofBytes> sendProof{};
    ASSERT_TRUE(proveBulletproofSend(c0, c1, 5, 7, r0, r1, sendProof));
    EXPECT_TRUE(verifyBulletproofSend(c0, c1, Slice(sendProof.data(), sendProof.size())));
    EXPECT_FALSE(proveBulletproofSend(pc0, c1, 0, 7, r0v, r1, sendProof));
    EXPECT_FALSE(verifyBulletproofSend(pc0, c1, Slice(agg.data(), agg.size())));

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

TEST(ConfidentialCrypto, ScalarPointAndSerializeHelpers)
{
    auto kp = makeKey();
    Scalar a = mustRandomScalar();
    Scalar b = mustRandomScalar();
    Scalar sum{};
    Scalar diff{};
    ASSERT_TRUE(addScalars(a, b, sum));
    ASSERT_TRUE(subScalars(sum, b, diff));
    EXPECT_EQ(diff, a);

    std::array<std::uint8_t, 32> sOut{};
    EXPECT_TRUE(serializeScalar(a, Slice(sOut.data(), sOut.size())));
    EXPECT_FALSE(serializeScalar(a, Slice(sOut.data(), 31)));

    std::array<std::uint8_t, 33> pOut{};
    EXPECT_TRUE(serializeCompressedPoint(kp.pk, Slice(pOut.data(), pOut.size())));
    EXPECT_EQ(std::memcmp(pOut.data(), kp.pk.data(), 33), 0);
    EXPECT_FALSE(serializeCompressedPoint(kp.pk, Slice(pOut.data(), 32)));
    CompressedPoint junk{};
    junk[0] = 0x02;
    EXPECT_FALSE(serializeCompressedPoint(junk, Slice(pOut.data(), pOut.size())));

    CompressedPoint sumP{};
    ASSERT_TRUE(pointAdd(kp.pk, kp.pk, sumP));
    CompressedPoint doubled{};
    Scalar two = amountToScalar(2);
    // 2 may not be a valid seckey verify in all contexts; use known scalar
    Scalar twoSk{};
    twoSk[31] = 2;
    ASSERT_TRUE(pointMul(kp.pk, twoSk, doubled));
    EXPECT_EQ(sumP, doubled);

    CompressedPoint diffP{};
    EXPECT_FALSE(pointSub(kp.pk, kp.pk, diffP));  // infinity

    CompressedPoint mulP{};
    ASSERT_TRUE(pointMul(kp.pk, a, mulP));
    EXPECT_FALSE(pointMul(junk, a, mulP));
    Scalar zero{};
    EXPECT_FALSE(pointMul(kp.pk, zero, mulP));
    EXPECT_FALSE(pointMulBase(zero, mulP));

    CompressedPoint tweaked{};
    ASSERT_TRUE(pointAddMulBase(kp.pk, a, tweaked));
    EXPECT_NE(tweaked, kp.pk);
    EXPECT_FALSE(pointAddMulBase(junk, a, tweaked));
    // Adding 0·G is a no-op; secp256k1 accepts a zero tweak.
    CompressedPoint same{};
    ASSERT_TRUE(pointAddMulBase(kp.pk, zero, same));
    EXPECT_EQ(same, kp.pk);
    Scalar orderTw{};
    unsigned char const kOrderTw[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
        0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
    std::memcpy(orderTw.data(), kOrderTw, 32);
    EXPECT_FALSE(pointAddMulBase(kp.pk, orderTw, tweaked));

    EXPECT_FALSE(pointAdd(junk, kp.pk, sumP));
    EXPECT_FALSE(pointAdd(kp.pk, junk, sumP));
    EXPECT_FALSE(pointSub(junk, kp.pk, diffP));
    EXPECT_FALSE(pointSub(kp.pk, junk, diffP));
    EXPECT_FALSE(pointMulBase(orderTw, mulP));

    EXPECT_FALSE(parseCompressedPoint(Slice(pOut.data(), 32), junk));

    CiphertextBytes raw{};
    Ciphertext ct{};
    ASSERT_TRUE(elgamalEncrypt(kp.pk, 1, a, ct));
    EXPECT_FALSE(serializeCiphertext(ct, Slice(raw.data(), 65)));
    ASSERT_TRUE(serializeCiphertext(ct, Slice(raw.data(), raw.size())));

    // Invalid ciphertext bytes fail re-parse inside serialize
    Ciphertext badCt = ct;
    badCt.c1[0] = 0x04;
    EXPECT_FALSE(serializeCiphertext(badCt, Slice(raw.data(), raw.size())));

    EXPECT_FALSE(elgamalEncrypt(junk, 1, a, ct));
    EXPECT_FALSE(elgamalEncrypt(kp.pk, 1, zero, ct));
    EXPECT_FALSE(elgamalRerandomize(ct, junk, a, ct));

    std::array<std::uint8_t, 4> pre{'t', 'e', 's', 't'};
    auto ctx = transactionContextID(Slice(pre.data(), pre.size()));
    EXPECT_FALSE(ctx.isZero());

    Scalar e{};
    EXPECT_FALSE(extractSigmaChallenge(Slice(sOut.data(), 16), e));
    ASSERT_TRUE(serializeScalar(a, Slice(sOut.data(), sOut.size())));
    // a is a valid seckey, so extract succeeds
    EXPECT_TRUE(extractSigmaChallenge(Slice(sOut.data(), sOut.size()), e));
    EXPECT_EQ(e, a);
    EXPECT_FALSE(extractSigmaChallenge(Slice(zero.data(), zero.size()), e));
}

TEST(ConfidentialCrypto, PedersenCommitScalarEdgeCases)
{
    Scalar r = mustRandomScalar();
    CompressedPoint pc{};
    ASSERT_TRUE(pedersenCommitScalar(amountToScalar(0), r, pc));
    CompressedPoint pc2{};
    ASSERT_TRUE(pedersenCommit(0, r, pc2));
    EXPECT_EQ(pc, pc2);

    Scalar badBlind{};
    EXPECT_FALSE(pedersenCommitScalar(amountToScalar(1), badBlind, pc));
    EXPECT_FALSE(pedersenCommit(1, badBlind, pc));

    // Amount equal to the group order is not a valid seckey
    Scalar orderAmt{};
    unsigned char const kOrder[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
        0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
    std::memcpy(orderAmt.data(), kOrder, 32);
    EXPECT_FALSE(pedersenCommitScalar(orderAmt, r, pc));

    ASSERT_TRUE(pedersenCommitScalar(amountToScalar(99), r, pc));
}

TEST(ConfidentialCrypto, SigmaProofFailurePaths)
{
    auto kp = makeKey();
    auto a = acct(1);
    auto id = issuance(2);
    auto ctx = transactionContextIDConvert(
        85, Slice(a.data(), a.size()), Slice(id.data(), id.size()), 1);

    SchnorrRegisterProof proof{};
    Scalar badSk{};
    EXPECT_FALSE(proveSchnorrRegister(
        badSk, kp.pk, Slice(ctx.data(), ctx.size()), proof));
    auto other = makeKey();
    EXPECT_FALSE(proveSchnorrRegister(
        other.sk, kp.pk, Slice(ctx.data(), ctx.size()), proof));

    ASSERT_TRUE(proveSchnorrRegister(
        kp.sk, kp.pk, Slice(ctx.data(), ctx.size()), proof));
    EXPECT_TRUE(extractSigmaChallenge(
        Slice(proof.data(), proof.size()), badSk));

    // Corrupt challenge / response
    auto badProof = proof;
    badProof[0] ^= 0xff;
    EXPECT_FALSE(verifySchnorrRegister(
        kp.pk, Slice(ctx.data(), ctx.size()), Slice(badProof.data(), badProof.size())));
    badProof = proof;
    std::memset(badProof.data(), 0, 32);  // zero challenge
    EXPECT_FALSE(verifySchnorrRegister(
        kp.pk, Slice(ctx.data(), ctx.size()), Slice(badProof.data(), badProof.size())));
    EXPECT_FALSE(verifySchnorrRegister(
        CompressedPoint{}, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    // Send sigma: empty recipients
    SendSigmaPublicInput emptyPub;
    emptyPub.senderKey = kp.pk;
    SendSigmaWitness wit;
    wit.amount = amountToScalar(1);
    wit.randomness = mustRandomScalar();
    wit.balance = amountToScalar(2);
    wit.balanceBlind = mustRandomScalar();
    wit.senderSk = kp.sk;
    SendSigmaProof sendProof{};
    EXPECT_FALSE(proveSendSigma(
        emptyPub, wit, Slice(ctx.data(), ctx.size()), sendProof));
    EXPECT_FALSE(verifySendSigma(
        emptyPub, Slice(ctx.data(), ctx.size()), Slice(sendProof.data(), sendProof.size())));

    // Valid send then truncate / corrupt
    auto recv = makeKey();
    std::uint64_t const m = 4;
    std::uint64_t const b = 9;
    Scalar r = mustRandomScalar();
    Scalar rho = mustRandomScalar();
    Scalar rb = mustRandomScalar();
    Ciphertext bal{};
    ASSERT_TRUE(elgamalEncrypt(kp.pk, b, rb, bal));

    SendSigmaPublicInput pub;
    pub.recipientKeys = {kp.pk, recv.pk};
    pub.senderKey = kp.pk;
    pub.c2.resize(2);
    for (std::size_t i = 0; i < pub.recipientKeys.size(); ++i)
    {
        Ciphertext ct{};
        ASSERT_TRUE(elgamalEncrypt(pub.recipientKeys[i], m, r, ct));
        pub.c1 = ct.c1;
        pub.c2[i] = ct.c2;
    }
    ASSERT_TRUE(pedersenCommit(m, r, pub.amountCommitment));
    ASSERT_TRUE(pedersenCommit(b, rho, pub.balanceCommitment));
    pub.balanceC1 = bal.c1;
    pub.balanceC2 = bal.c2;
    wit.amount = amountToScalar(m);
    wit.randomness = r;
    wit.balance = amountToScalar(b);
    wit.balanceBlind = rho;
    ASSERT_TRUE(proveSendSigma(pub, wit, Slice(ctx.data(), ctx.size()), sendProof));
    EXPECT_FALSE(verifySendSigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(sendProof.data(), 100)));

    auto corrupted = sendProof;
    std::memset(corrupted.data() + 32, 0, 32);  // zm = 0 field element OK
    // zero zr (blinding response) rejected by parseScalar
    std::memset(corrupted.data() + 64, 0, 32);
    EXPECT_FALSE(verifySendSigma(
        pub,
        Slice(ctx.data(), ctx.size()),
        Slice(corrupted.data(), corrupted.size())));

    // Size mismatch between keys and c2
    pub.c2.pop_back();
    EXPECT_FALSE(proveSendSigma(pub, wit, Slice(ctx.data(), ctx.size()), sendProof));
    EXPECT_FALSE(verifySendSigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(sendProof.data(), sendProof.size())));

    wit.randomness = {};
    pub.c2.push_back(pub.c1);
    EXPECT_FALSE(proveSendSigma(pub, wit, Slice(ctx.data(), ctx.size()), sendProof));

    // ConvertBack failures
    ConvertBackSigmaPublicInput cpub;
    cpub.holderKey = kp.pk;
    cpub.balanceC1 = bal.c1;
    cpub.balanceC2 = bal.c2;
    ASSERT_TRUE(pedersenCommit(b, rho, cpub.balanceCommitment));
    ConvertBackSigmaWitness cwit;
    cwit.balance = amountToScalar(b);
    cwit.balanceBlind = {};
    cwit.holderSk = kp.sk;
    ConvertBackSigmaProof cproof{};
    EXPECT_FALSE(proveConvertBackSigma(
        cpub, cwit, Slice(ctx.data(), ctx.size()), cproof));
    cwit.balanceBlind = rho;
    ASSERT_TRUE(proveConvertBackSigma(
        cpub, cwit, Slice(ctx.data(), ctx.size()), cproof));
    EXPECT_FALSE(verifyConvertBackSigma(
        cpub, Slice(ctx.data(), ctx.size()), Slice(cproof.data(), 32)));
    auto cBad = cproof;
    std::memset(cBad.data(), 0, 32);
    EXPECT_FALSE(verifyConvertBackSigma(
        cpub, Slice(ctx.data(), ctx.size()), Slice(cBad.data(), cBad.size())));
    cpub.holderKey = {};
    EXPECT_FALSE(verifyConvertBackSigma(
        cpub, Slice(ctx.data(), ctx.size()), Slice(cproof.data(), cproof.size())));

    // Clawback amount 0 rejected; wrong sk rejected; short proof rejected
    ClawbackSigmaPublicInput claw;
    claw.issuerKey = kp.pk;
    ASSERT_TRUE(elgamalEncrypt(kp.pk, 0, r, claw.issuerBalance));
    claw.revealedAmount = 0;
    ClawbackSigmaProof clawProof{};
    EXPECT_FALSE(proveClawbackSigma(
        claw, kp.sk, Slice(ctx.data(), ctx.size()), clawProof));
    EXPECT_FALSE(verifyClawbackSigma(
        claw, Slice(ctx.data(), ctx.size()), Slice(clawProof.data(), clawProof.size())));

    ASSERT_TRUE(elgamalEncrypt(kp.pk, 11, r, claw.issuerBalance));
    claw.revealedAmount = 11;
    EXPECT_FALSE(proveClawbackSigma(
        claw, other.sk, Slice(ctx.data(), ctx.size()), clawProof));
    EXPECT_FALSE(proveClawbackSigma(
        claw, Scalar{}, Slice(ctx.data(), ctx.size()), clawProof));
    ASSERT_TRUE(proveClawbackSigma(
        claw, kp.sk, Slice(ctx.data(), ctx.size()), clawProof));
    EXPECT_FALSE(verifyClawbackSigma(
        claw, Slice(ctx.data(), ctx.size()), Slice(clawProof.data(), 16)));

    // Amount-zero verify path (size-valid proof, rejected before transcript)
    {
        ClawbackSigmaPublicInput clawZero = claw;
        clawZero.revealedAmount = 0;
        EXPECT_FALSE(verifyClawbackSigma(
            clawZero,
            Slice(ctx.data(), ctx.size()),
            Slice(clawProof.data(), clawProof.size())));
    }

    claw.issuerBalance.c1 = {};
    EXPECT_FALSE(verifyClawbackSigma(
        claw, Slice(ctx.data(), ctx.size()), Slice(clawProof.data(), clawProof.size())));

    // Restore a valid clawback public input for further negative tests
    ASSERT_TRUE(elgamalEncrypt(kp.pk, 11, r, claw.issuerBalance));
    claw.issuerKey = kp.pk;
    claw.revealedAmount = 11;

    // Invalid public points force reconstruct / verify failures
    // Restore send pub shape (keys/c2 length match) with original c2 material.
    pub.recipientKeys = {kp.pk, recv.pk};
    pub.senderKey = kp.pk;
    pub.c2.resize(2);
    for (std::size_t i = 0; i < pub.recipientKeys.size(); ++i)
    {
        Ciphertext ct{};
        ASSERT_TRUE(elgamalEncrypt(pub.recipientKeys[i], m, r, ct));
        pub.c1 = ct.c1;
        pub.c2[i] = ct.c2;
    }
    ASSERT_TRUE(pedersenCommit(m, r, pub.amountCommitment));
    ASSERT_TRUE(pedersenCommit(b, rho, pub.balanceCommitment));
    pub.balanceC1 = bal.c1;
    pub.balanceC2 = bal.c2;
    wit.randomness = r;
    ASSERT_TRUE(proveSendSigma(pub, wit, Slice(ctx.data(), ctx.size()), sendProof));

    SendSigmaPublicInput badPub = pub;
    badPub.senderKey = {};
    EXPECT_FALSE(verifySendSigma(
        badPub,
        Slice(ctx.data(), ctx.size()),
        Slice(sendProof.data(), sendProof.size())));
    badPub = pub;
    badPub.balanceC1 = {};
    EXPECT_FALSE(verifySendSigma(
        badPub,
        Slice(ctx.data(), ctx.size()),
        Slice(sendProof.data(), sendProof.size())));
    badPub = pub;
    badPub.recipientKeys[0] = {};
    EXPECT_FALSE(verifySendSigma(
        badPub,
        Slice(ctx.data(), ctx.size()),
        Slice(sendProof.data(), sendProof.size())));

    cpub.holderKey = kp.pk;
    cpub.balanceC1 = bal.c1;
    cpub.balanceC2 = bal.c2;
    ASSERT_TRUE(pedersenCommit(b, rho, cpub.balanceCommitment));
    ConvertBackSigmaPublicInput badC = cpub;
    badC.balanceCommitment = {};
    EXPECT_FALSE(verifyConvertBackSigma(
        badC, Slice(ctx.data(), ctx.size()), Slice(cproof.data(), cproof.size())));
    badC = cpub;
    badC.balanceC2 = {};
    EXPECT_FALSE(verifyConvertBackSigma(
        badC, Slice(ctx.data(), ctx.size()), Slice(cproof.data(), cproof.size())));

    ClawbackSigmaPublicInput clawBad = claw;
    clawBad.issuerKey = {};
    EXPECT_FALSE(verifyClawbackSigma(
        clawBad,
        Slice(ctx.data(), ctx.size()),
        Slice(clawProof.data(), clawProof.size())));
    clawBad = claw;
    clawBad.issuerBalance.c2 = {};
    EXPECT_FALSE(verifyClawbackSigma(
        clawBad,
        Slice(ctx.data(), ctx.size()),
        Slice(clawProof.data(), clawProof.size())));
}

TEST(ConfidentialCrypto, BulletproofVerifyRejectsMalformed)
{
    Scalar r = mustRandomScalar();
    CompressedPoint pc{};
    ASSERT_TRUE(pedersenCommit(6, r, pc));
    std::array<std::uint8_t, kSingleBulletproofBytes> proof{};
    ASSERT_TRUE(proveBulletproofSingle(pc, 6, r, proof));

    // Wrong length
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(proof.data(), proof.size() - 1)));
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(proof.data(), 0)));

    // Invalid compressed point in A (first 33 bytes)
    auto bad = proof;
    bad[0] = 0x04;
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(bad.data(), bad.size())));

    // Invalid point in an L vector slot (after A S T1 T2)
    bad = proof;
    std::size_t const l0 = 4 * kCompressedPointBytes;
    bad[l0] = 0x04;
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(bad.data(), bad.size())));

    // Flip a bit in tau_x region — proof parses but equation fails
    bad = proof;
    std::size_t const scalarOff =
        4 * kCompressedPointBytes + 2 * 6 * kCompressedPointBytes;
    bad[scalarOff] ^= 0x01;
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(bad.data(), bad.size())));

    // tHat = 0 exercises commitGplusH with only the H coefficient
    bad = proof;
    std::size_t const tHatOff = scalarOff + 2 * kScalarBytes;
    std::memset(bad.data() + tHatOff, 0, kScalarBytes);
    EXPECT_FALSE(verifyBulletproofSingle(pc, Slice(bad.data(), bad.size())));

    // Wrong commitment
    CompressedPoint other{};
    Scalar r2 = mustRandomScalar();
    ASSERT_TRUE(pedersenCommit(6, r2, other));
    EXPECT_FALSE(verifyBulletproofSingle(other, Slice(proof.data(), proof.size())));
    CompressedPoint junkPk{};
    junkPk[0] = 0x02;
    EXPECT_FALSE(verifyBulletproofSingle(junkPk, Slice(proof.data(), proof.size())));

    // Aggregated malformed
    Scalar r0 = mustRandomScalar();
    Scalar r1 = mustRandomScalar();
    CompressedPoint c0{};
    CompressedPoint c1{};
    ASSERT_TRUE(pedersenCommit(1, r0, c0));
    ASSERT_TRUE(pedersenCommit(2, r1, c1));
    std::array<std::uint8_t, kAggregatedBulletproofBytes> agg{};
    ASSERT_TRUE(proveBulletproofAggregated(c0, c1, 1, 2, r0, r1, agg));
    EXPECT_FALSE(verifyBulletproofAggregated(
        c0, c1, Slice(agg.data(), agg.size() - 1)));
    auto aggBad = agg;
    aggBad[0] = 0x04;
    EXPECT_FALSE(verifyBulletproofAggregated(
        c0, c1, Slice(aggBad.data(), aggBad.size())));
    // Corrupt L point in aggregated proof
    aggBad = agg;
    aggBad[4 * kCompressedPointBytes] = 0x04;
    EXPECT_FALSE(verifyBulletproofAggregated(
        c0, c1, Slice(aggBad.data(), aggBad.size())));
    EXPECT_FALSE(verifyBulletproofAggregated(
        junkPk, c1, Slice(agg.data(), agg.size())));

    // Value at top of 64-bit range (Bünz n=64 bit decomposition)
    Scalar rMax = mustRandomScalar();
    CompressedPoint pcMax{};
    std::uint64_t const vmax = std::numeric_limits<std::uint64_t>::max();
    ASSERT_TRUE(pedersenCommit(vmax, rMax, pcMax));
    std::array<std::uint8_t, kSingleBulletproofBytes> pMax{};
    ASSERT_TRUE(proveBulletproofSingle(pcMax, vmax, rMax, pMax));
    EXPECT_TRUE(verifyBulletproofSingle(pcMax, Slice(pMax.data(), pMax.size())));
}

TEST(ConfidentialCrypto, AuditorEqualitySigmaRoundTrip)
{
    auto issuer = makeKey();
    auto auditor = makeKey();
    std::uint64_t const m = 42;
    Scalar rIss = mustRandomScalar();
    Scalar rAud = mustRandomScalar();

    Ciphertext issuerCt{};
    Ciphertext auditorCt{};
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, m, rIss, issuerCt));
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, m, rAud, auditorCt));

    AuditorEqualitySigmaPublicInput pub;
    pub.issuerKey = issuer.pk;
    pub.issuerCiphertext = issuerCt;
    pub.auditorKey = auditor.pk;
    pub.auditorCiphertext = auditorCt;

    AuditorEqualitySigmaWitness wit;
    wit.issuerSk = issuer.sk;
    wit.randomness = rAud;
    wit.amount = amountToScalar(m);

    auto account = acct(0xA1);
    auto holder = acct(0xB2);
    auto id = issuance(0xC3);
    auto ctx = transactionContextIDMigrateAuditor(
        90,
        Slice(account.data(), account.size()),
        Slice(id.data(), id.size()),
        11,
        Slice(holder.data(), holder.size()),
        3);

    AuditorEqualitySigmaProof proof{};
    ASSERT_TRUE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    EXPECT_EQ(proof.size(), kAuditorEqualitySigmaProofBytes);
    EXPECT_TRUE(verifyAuditorEqualitySigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    // Zero plaintext also accepted (empty confidential balance migration).
    Ciphertext issuerZero{};
    Ciphertext auditorZero{};
    Scalar rAud0 = mustRandomScalar();
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, 0, rIss, issuerZero));
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, 0, rAud0, auditorZero));
    pub.issuerCiphertext = issuerZero;
    pub.auditorCiphertext = auditorZero;
    wit.randomness = rAud0;
    wit.amount = amountToScalar(0);
    AuditorEqualitySigmaProof proof0{};
    ASSERT_TRUE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof0));
    EXPECT_TRUE(verifyAuditorEqualitySigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof0.data(), proof0.size())));
}

TEST(ConfidentialCrypto, AuditorEqualitySigmaRejectsTamperAndWrongBinding)
{
    auto issuer = makeKey();
    auto auditor = makeKey();
    std::uint64_t const m = 17;
    Scalar rIss = mustRandomScalar();
    Scalar rAud = mustRandomScalar();

    Ciphertext issuerCt{};
    Ciphertext auditorCt{};
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, m, rIss, issuerCt));
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, m, rAud, auditorCt));

    AuditorEqualitySigmaPublicInput pub;
    pub.issuerKey = issuer.pk;
    pub.issuerCiphertext = issuerCt;
    pub.auditorKey = auditor.pk;
    pub.auditorCiphertext = auditorCt;

    AuditorEqualitySigmaWitness wit;
    wit.issuerSk = issuer.sk;
    wit.randomness = rAud;
    wit.amount = amountToScalar(m);

    auto account = acct(0x11);
    auto holder = acct(0x22);
    auto id = issuance(0x33);
    auto ctx = transactionContextIDMigrateAuditor(
        90,
        Slice(account.data(), account.size()),
        Slice(id.data(), id.size()),
        1,
        Slice(holder.data(), holder.size()),
        7);

    AuditorEqualitySigmaProof proof{};
    ASSERT_TRUE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    ASSERT_TRUE(verifyAuditorEqualitySigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof.data(), proof.size())));

    // Tamper each compact field (e, zx, zr, zm).
    for (std::size_t off = 0; off < kAuditorEqualitySigmaProofBytes; off += 32)
    {
        auto bad = proof;
        bad[off] ^= 0x01;
        EXPECT_FALSE(verifyAuditorEqualitySigma(
            pub,
            Slice(ctx.data(), ctx.size()),
            Slice(bad.data(), bad.size())));
    }

    // Zero challenge rejected ([1, n-1]).
    auto zeroE = proof;
    std::memset(zeroE.data(), 0, 32);
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(zeroE.data(), zeroE.size())));

    // Truncation rejected.
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        pub, Slice(ctx.data(), ctx.size()), Slice(proof.data(), 127)));

    // Wrong context (target auditor version) rejected.
    auto ctxBad = transactionContextIDMigrateAuditor(
        90,
        Slice(account.data(), account.size()),
        Slice(id.data(), id.size()),
        1,
        Slice(holder.data(), holder.size()),
        8);
    EXPECT_NE(ctx, ctxBad);
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        pub,
        Slice(ctxBad.data(), ctxBad.size()),
        Slice(proof.data(), proof.size())));

    // Wrong holder in context rejected.
    auto holder2 = acct(0x99);
    auto ctxHolder = transactionContextIDMigrateAuditor(
        90,
        Slice(account.data(), account.size()),
        Slice(id.data(), id.size()),
        1,
        Slice(holder2.data(), holder2.size()),
        7);
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        pub,
        Slice(ctxHolder.data(), ctxHolder.size()),
        Slice(proof.data(), proof.size())));

    // Wrong pending auditor key rejected.
    auto otherAud = makeKey();
    auto badKeyPub = pub;
    badKeyPub.auditorKey = otherAud.pk;
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        badKeyPub,
        Slice(ctx.data(), ctx.size()),
        Slice(proof.data(), proof.size())));

    // Wrong issuer key rejected.
    auto otherIss = makeKey();
    auto badIssPub = pub;
    badIssPub.issuerKey = otherIss.pk;
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        badIssPub,
        Slice(ctx.data(), ctx.size()),
        Slice(proof.data(), proof.size())));

    // Wrong auditor ciphertext (different randomness / amount) rejected.
    Ciphertext wrongAud{};
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, m, mustRandomScalar(), wrongAud));
    auto badCtPub = pub;
    badCtPub.auditorCiphertext = wrongAud;
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        badCtPub,
        Slice(ctx.data(), ctx.size()),
        Slice(proof.data(), proof.size())));

    Ciphertext wrongIss{};
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, m + 1, rIss, wrongIss));
    badCtPub = pub;
    badCtPub.issuerCiphertext = wrongIss;
    EXPECT_FALSE(verifyAuditorEqualitySigma(
        badCtPub,
        Slice(ctx.data(), ctx.size()),
        Slice(proof.data(), proof.size())));
}

TEST(ConfidentialCrypto, AuditorEqualitySigmaProveRejectsBadWitness)
{
    auto issuer = makeKey();
    auto auditor = makeKey();
    std::uint64_t const m = 9;
    Scalar rIss = mustRandomScalar();
    Scalar rAud = mustRandomScalar();

    Ciphertext issuerCt{};
    Ciphertext auditorCt{};
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, m, rIss, issuerCt));
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, m, rAud, auditorCt));

    AuditorEqualitySigmaPublicInput pub;
    pub.issuerKey = issuer.pk;
    pub.issuerCiphertext = issuerCt;
    pub.auditorKey = auditor.pk;
    pub.auditorCiphertext = auditorCt;

    auto account = acct(0x44);
    auto holder = acct(0x55);
    auto id = issuance(0x66);
    auto ctx = transactionContextIDMigrateAuditor(
        90,
        Slice(account.data(), account.size()),
        Slice(id.data(), id.size()),
        2,
        Slice(holder.data(), holder.size()),
        1);

    AuditorEqualitySigmaWitness wit;
    wit.issuerSk = issuer.sk;
    wit.randomness = rAud;
    wit.amount = amountToScalar(m);

    AuditorEqualitySigmaProof proof{};
    // Wrong issuer secret
    wit.issuerSk = mustRandomScalar();
    EXPECT_FALSE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    wit.issuerSk = issuer.sk;

    // Wrong randomness
    wit.randomness = mustRandomScalar();
    EXPECT_FALSE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    wit.randomness = rAud;

    // Wrong amount
    wit.amount = amountToScalar(m + 1);
    EXPECT_FALSE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    wit.amount = amountToScalar(m);

    // Zero / invalid secrets
    wit.issuerSk = {};
    EXPECT_FALSE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
    wit.issuerSk = issuer.sk;
    wit.randomness = {};
    EXPECT_FALSE(proveAuditorEqualitySigma(
        pub, wit, Slice(ctx.data(), ctx.size()), proof));
}
