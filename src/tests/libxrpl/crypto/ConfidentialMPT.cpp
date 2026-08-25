#include <helpers/TestFamily.h>
#include <helpers/TestSink.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

using namespace xrpl;
using namespace xrpl::confidential;

namespace {

Scalar
mustRandomScalar()
{
    static std::uint32_t counter = 900001;
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

AccountID
acctId(std::uint8_t tag)
{
    AccountID id;
    id.data()[0] = tag;
    for (std::size_t i = 1; i < id.size(); ++i)
        id.data()[i] = static_cast<std::uint8_t>(tag + i);
    return id;
}

STTx
makeBareTx()
{
    return STTx(ttACCOUNT_SET, [](STObject& obj) {
        obj[sfAccount] = acctId(0x11);
        obj[sfFee] = STAmount{XRPAmount{10}};
        obj[sfSequence] = 1;
        obj.setFieldVL(sfSigningPubKey, Slice{});
    });
}

STTx
makeTxWithSigners(std::size_t n)
{
    return STTx(ttACCOUNT_SET, [n](STObject& obj) {
        obj[sfAccount] = acctId(0x22);
        obj[sfFee] = STAmount{XRPAmount{10}};
        obj[sfSequence] = 1;
        obj.setFieldVL(sfSigningPubKey, Slice{});
        STArray signers{sfSigners};
        for (std::size_t i = 0; i < n; ++i)
        {
            STObject signer{sfSigner};
            signer[sfAccount] = acctId(static_cast<std::uint8_t>(0x30 + i));
            signer.setFieldVL(sfSigningPubKey, Slice{});
            signer.setFieldVL(sfTxnSignature, Slice{});
            signers.push_back(std::move(signer));
        }
        obj.setFieldArray(sfSigners, signers);
    });
}

SLE
makeIssuance(AccountID const& issuer, std::uint32_t seq)
{
    SLE sle{keylet::mptIssuance(seq, issuer)};
    sle[sfIssuer] = issuer;
    sle[sfSequence] = seq;
    return sle;
}

}  // namespace

TEST(ConfidentialMPTHelpers, PreflightAndParseRejectMalformed)
{
    EXPECT_EQ(preflightCiphertext(Slice(nullptr, 0)), temBAD_CIPHERTEXT);
    std::array<std::uint8_t, 65> shortCt{};
    EXPECT_EQ(
        preflightCiphertext(Slice(shortCt.data(), shortCt.size())),
        temBAD_CIPHERTEXT);

    Ciphertext ct{};
    EXPECT_EQ(
        parseCiphertextField(Slice(shortCt.data(), shortCt.size()), ct),
        tecBAD_PROOF);

    std::array<std::uint8_t, 33> badPt{};
    badPt[0] = 0x04;
    EXPECT_EQ(preflightPoint33(Slice(badPt.data(), badPt.size())), temMALFORMED);
    badPt[0] = 0x02;
    EXPECT_EQ(preflightPoint33(Slice(badPt.data(), badPt.size())), temMALFORMED);
}

TEST(ConfidentialMPTHelpers, PreflightAcceptsValidCiphertextAndPoint)
{
    auto kp = makeKey();
    Scalar r = mustRandomScalar();
    Ciphertext ct{};
    ASSERT_TRUE(elgamalEncrypt(kp.pk, 3, r, ct));
    CiphertextBytes raw{};
    ASSERT_TRUE(serializeCiphertext(ct, Slice(raw.data(), raw.size())));
    EXPECT_EQ(preflightCiphertext(Slice(raw.data(), raw.size())), tesSUCCESS);

    Ciphertext parsed{};
    EXPECT_EQ(
        parseCiphertextField(Slice(raw.data(), raw.size()), parsed), tesSUCCESS);
    EXPECT_EQ(parsed.c1, ct.c1);
    EXPECT_EQ(parsed.c2, ct.c2);

    EXPECT_EQ(preflightPoint33(Slice(kp.pk.data(), kp.pk.size())), tesSUCCESS);
}

TEST(ConfidentialMPTHelpers, SameC1AndEncZeroFor)
{
    auto holder = makeKey();
    auto other = makeKey();
    Scalar r = mustRandomScalar();
    Ciphertext a{};
    Ciphertext b{};
    ASSERT_TRUE(elgamalEncrypt(holder.pk, 1, r, a));
    ASSERT_TRUE(elgamalEncrypt(other.pk, 1, r, b));
    EXPECT_TRUE(sameC1(a, b));
    EXPECT_FALSE(sameC1(a, Ciphertext{}));

    test::TestFamily family{beast::Journal{TestSink::instance()}};
    Rules rules{std::unordered_set<uint256, beast::Uhash<>>{}};
    Fees fees{XRPAmount{10}, XRPAmount{0}, XRPAmount{0}};
    Ledger view{kCreateGenesis, rules, fees, {}, family};

    auto const issuer = acctId(0xAB);
    auto issuance = makeIssuance(issuer, 7);
    auto const holderAcct = acctId(0xCD);
    Ciphertext ez{};
    EXPECT_EQ(encZeroFor(view, issuance, holderAcct, holder.pk, ez), tesSUCCESS);

    CompressedPoint badPk{};
    badPk[0] = 0x02;
    Ciphertext fail{};
    EXPECT_EQ(
        encZeroFor(view, issuance, holderAcct, badPk, fail), tecINTERNAL);
}

TEST(ConfidentialMPTHelpers, VersionWrapAndSetCiphertextField)
{
    auto const holder = acctId(0x01);
    auto const issuer = acctId(0x02);
    auto const mptId = makeMptID(1, issuer);
    auto const index = keylet::mptoken(mptId, holder).key;
    SLE mpt{ltMPTOKEN, index};
    EXPECT_FALSE(mpt[~sfConfidentialBalanceVersion]);
    incrementConfidentialVersion(mpt);
    EXPECT_EQ(mpt[sfConfidentialBalanceVersion], 1u);
    incrementConfidentialVersion(mpt);
    EXPECT_EQ(mpt[sfConfidentialBalanceVersion], 2u);

    mpt[sfConfidentialBalanceVersion] = std::numeric_limits<std::uint32_t>::max();
    incrementConfidentialVersion(mpt);
    EXPECT_EQ(mpt[sfConfidentialBalanceVersion], 0u);

    auto kp = makeKey();
    Scalar r = mustRandomScalar();
    Ciphertext ct{};
    ASSERT_TRUE(elgamalEncrypt(kp.pk, 9, r, ct));
    setCiphertextField(mpt, sfConfidentialBalanceSpending, ct);
    auto const blob = mpt.getFieldVL(sfConfidentialBalanceSpending);
    Ciphertext parsed{};
    ASSERT_TRUE(parseCiphertext(Slice(blob.data(), blob.size()), parsed));
    EXPECT_EQ(parsed.c1, ct.c1);
    EXPECT_EQ(parsed.c2, ct.c2);

    auto issuance = makeIssuance(issuer, 1);
    issuance[sfConfidentialHolderCount] = 1;
    mpt.setFieldVL(sfHolderEncryptionKey, Slice(kp.pk.data(), kp.pk.size()));
    setCiphertextField(mpt, sfConfidentialBalanceInbox, ct);
    setCiphertextField(mpt, sfIssuerEncryptedBalance, ct);
    mpt[sfAuditorKeyVersion] = 1;
    EXPECT_EQ(clearConfidentialState(issuance, mpt), tesSUCCESS);
    EXPECT_EQ(issuance[sfConfidentialHolderCount], 0u);
    EXPECT_FALSE(mpt.isFieldPresent(sfHolderEncryptionKey));
    EXPECT_FALSE(mpt.isFieldPresent(sfConfidentialBalanceSpending));
    EXPECT_FALSE(mpt.isFieldPresent(sfConfidentialBalanceInbox));
    EXPECT_FALSE(mpt.isFieldPresent(sfIssuerEncryptedBalance));
    EXPECT_FALSE(mpt.isFieldPresent(sfAuditorKeyVersion));
    EXPECT_EQ(clearConfidentialState(issuance, mpt), tecINTERNAL);
}

TEST(ConfidentialMPTHelpers, BaseFeeWithAndWithoutSigners)
{
    test::TestFamily family{beast::Journal{TestSink::instance()}};
    Rules rules{std::unordered_set<uint256, beast::Uhash<>>{}};
    Fees fees{XRPAmount{10}, XRPAmount{0}, XRPAmount{0}};
    Ledger view{kCreateGenesis, rules, fees, {}, family};

    auto const bare = makeBareTx();
    EXPECT_EQ(
        confidentialMptBaseFee(view, bare),
        XRPAmount{10} * kConfidentialTransferFeeMultiplier);

    auto const multi = makeTxWithSigners(2);
    // (base + 2*base) * multiplier = 3 * 10 * 10
    EXPECT_EQ(
        confidentialMptBaseFee(view, multi),
        XRPAmount{300});
}

TEST(ConfidentialMPTHelpers, CheckPlaintextCiphertextsPaths)
{
    auto holder = makeKey();
    auto issuer = makeKey();
    auto auditor = makeKey();
    Scalar r = mustRandomScalar();
    std::uint64_t const amount = 42;

    Ciphertext holderCt{};
    Ciphertext issuerCt{};
    Ciphertext auditorCt{};
    ASSERT_TRUE(elgamalEncrypt(holder.pk, amount, r, holderCt));
    ASSERT_TRUE(elgamalEncrypt(issuer.pk, amount, r, issuerCt));
    ASSERT_TRUE(elgamalEncrypt(auditor.pk, amount, r, auditorCt));

    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            std::nullopt,
            holderCt,
            issuerCt,
            std::nullopt),
        tesSUCCESS);

    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            auditor.pk,
            holderCt,
            issuerCt,
            auditorCt),
        tesSUCCESS);

    // Missing auditor ciphertext when auditor key present
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            auditor.pk,
            holderCt,
            issuerCt,
            std::nullopt),
        tecNO_PERMISSION);

    // Wrong holder ciphertext
    Ciphertext wrongH = holderCt;
    wrongH.c2[32] ^= 0x01;
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            std::nullopt,
            wrongH,
            issuerCt,
            std::nullopt),
        tecBAD_PROOF);

    // Wrong issuer ciphertext
    Ciphertext wrongI = issuerCt;
    wrongI.c2[32] ^= 0x01;
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            std::nullopt,
            holderCt,
            wrongI,
            std::nullopt),
        tecBAD_PROOF);

    // Divergent C1 values (different randomness)
    Scalar r2 = mustRandomScalar();
    Ciphertext holderAlt{};
    ASSERT_TRUE(elgamalEncrypt(holder.pk, amount, r2, holderAlt));
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            std::nullopt,
            holderAlt,
            issuerCt,
            std::nullopt),
        tecBAD_PROOF);

    // Bad auditor ciphertext
    Ciphertext wrongA = auditorCt;
    wrongA.c2[32] ^= 0x01;
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            auditor.pk,
            holderCt,
            issuerCt,
            wrongA),
        tecBAD_PROOF);

    // Invalid holder pk makes expected encryption fail
    CompressedPoint badPk{};
    badPk[0] = 0x02;
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            badPk,
            issuer.pk,
            std::nullopt,
            holderCt,
            issuerCt,
            std::nullopt),
        tecBAD_PROOF);

    // Invalid auditor pk with otherwise valid inputs
    EXPECT_EQ(
        checkPlaintextCiphertexts(
            amount,
            r,
            holder.pk,
            issuer.pk,
            badPk,
            holderCt,
            issuerCt,
            auditorCt),
        tecBAD_PROOF);
}
