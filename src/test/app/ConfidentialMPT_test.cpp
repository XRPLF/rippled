#include <test/jtx.h>
#include <test/jtx/mpt.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/crypto/confidential.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace xrpl::test {

class ConfidentialMPT_test : public beast::unit_test::Suite
{
    using Scalar = confidential::Scalar;
    using CompressedPoint = confidential::CompressedPoint;
    using Ciphertext = confidential::Ciphertext;

    struct Keypair
    {
        Scalar sk{};
        CompressedPoint pk{};
    };

    static Scalar
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
            if (confidential::pointMulBase(sk, pk))
                return sk;
        }
        return {};
    }

    static Keypair
    makeKey()
    {
        Keypair kp;
        kp.sk = mustRandomScalar();
        confidential::pointMulBase(kp.sk, kp.pk);
        return kp;
    }

    static std::string
    hexOf(Slice s)
    {
        return strHex(s);
    }

    template <std::size_t N>
    static std::string
    hexOf(std::array<std::uint8_t, N> const& a)
    {
        return strHex(Slice(a.data(), a.size()));
    }

    static std::string
    hexPoint(CompressedPoint const& p)
    {
        return hexOf(p);
    }

    static std::string
    hexCipher(Ciphertext const& ct)
    {
        confidential::CiphertextBytes raw{};
        confidential::serializeCiphertext(ct, Slice(raw.data(), raw.size()));
        return hexOf(raw);
    }

    static uint256
    scalarToUint(Scalar const& s)
    {
        return uint256::fromVoid(s.data());
    }

    static Ciphertext
    mustEncrypt(CompressedPoint const& pk, std::uint64_t amount, Scalar const& r)
    {
        Ciphertext ct{};
        confidential::elgamalEncrypt(pk, amount, r, ct);
        return ct;
    }

    void
    testLifecycle(FeatureBitset features)
    {
        testcase("confidential convert merge send convert-back clawback");

        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const carol("carol");
        Env env(*this, features);
        env.fund(XRP(10'000), gw, alice, carol);

        MPTTester mpt(env, gw, {.holders = {alice, carol}});
        mpt.create(
            {.pay = {{std::vector<Account>{alice, carol}, 50}},
             .flags = tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer | tfMPTCanClawback});

        auto issuer = makeKey();
        mpt.set(
            {.account = gw,
             .issuerEncryptionKey = std::string(
                 reinterpret_cast<char const*>(issuer.pk.data()), issuer.pk.size())});

        auto aliceKp = makeKey();
        auto carolKp = makeKey();
        auto const id = mpt.issuanceID();

        auto submitConvert = [&](Account const& acct,
                                 Keypair const& kp,
                                 std::uint64_t amount,
                                 bool initializing) {
            Scalar r = mustRandomScalar();
            auto holderCt = mustEncrypt(kp.pk, amount, r);
            auto issuerCt = mustEncrypt(issuer.pk, amount, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = acct.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = std::to_string(amount);
            jv[sfHolderEncryptedAmount] = hexCipher(holderCt);
            jv[sfIssuerEncryptedAmount] = hexCipher(issuerCt);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            if (initializing)
            {
                jv[sfHolderEncryptionKey] = hexPoint(kp.pk);
                auto const ctxId = confidential::transactionContextIDConvert(
                    static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                    Slice(acct.id().data(), acct.id().size()),
                    Slice(id.data(), id.size()),
                    env.seq(acct));
                confidential::SchnorrRegisterProof proof{};
                BEAST_EXPECT(confidential::proveSchnorrRegister(
                    kp.sk, kp.pk, Slice(ctxId.data(), ctxId.size()), proof));
                jv[sfZKProof] = hexOf(proof);
            }
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        };

        submitConvert(alice, aliceKp, 40, true);
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 10));
        {
            auto const sle = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(sle && (*sle)[sfConfidentialOutstandingAmount] == 40);
        }

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        submitConvert(carol, carolKp, 0, true);

        {
            json::Value bad;
            bad[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            bad[jss::Account] = alice.human();
            bad[sfMPTokenIssuanceID] = to_string(id);
            bad[sfMPTAmount] = "0";
            confidential::CiphertextBytes z{};
            bad[sfHolderEncryptedAmount] = hexOf(z);
            bad[sfIssuerEncryptedAmount] = hexOf(z);
            bad[sfBlindingFactor] = to_string(uint256{});
            bad[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> dummy{};
            bad[sfZKProof] = hexOf(dummy);
            env(bad, ter(temBAD_AMOUNT));
        }

        Scalar rAmt = mustRandomScalar();
        Scalar rho = mustRandomScalar();
        std::uint64_t const sendAmt = 10;
        std::uint64_t const bal = 40;
        auto encSender = mustEncrypt(aliceKp.pk, sendAmt, rAmt);
        auto encDest = mustEncrypt(carolKp.pk, sendAmt, rAmt);
        auto encIss = mustEncrypt(issuer.pk, sendAmt, rAmt);
        CompressedPoint pcM{};
        CompressedPoint pcB{};
        BEAST_EXPECT(confidential::pedersenCommit(sendAmt, rAmt, pcM));
        BEAST_EXPECT(confidential::pedersenCommit(bal, rho, pcB));
        CompressedPoint pcRem{};
        BEAST_EXPECT(confidential::pointSub(pcB, pcM, pcRem));
        Scalar remBlind{};
        BEAST_EXPECT(confidential::subScalars(rho, rAmt, remBlind));

        confidential::SendSigmaPublicInput pub;
        pub.recipientKeys = {aliceKp.pk, carolKp.pk, issuer.pk};
        pub.senderKey = aliceKp.pk;
        pub.c1 = encSender.c1;
        pub.c2 = {encSender.c2, encDest.c2, encIss.c2};
        pub.amountCommitment = pcM;
        pub.balanceCommitment = pcB;
        auto const sleAlice = env.le(keylet::mptoken(id, alice.id()));
        BEAST_EXPECT(sleAlice);
        Ciphertext spending{};
        BEAST_EXPECT(confidential::parseCiphertext((*sleAlice)[sfConfidentialBalanceSpending], spending));
        pub.balanceC1 = spending.c1;
        pub.balanceC2 = spending.c2;

        auto const sendCtx = confidential::transactionContextIDSend(
            static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_SEND),
            Slice(alice.id().data(), alice.id().size()),
            Slice(id.data(), id.size()),
            env.seq(alice),
            Slice(carol.id().data(), carol.id().size()),
            (*sleAlice)[~sfConfidentialBalanceVersion].value_or(0));

        confidential::SendSigmaWitness wit;
        wit.amount = confidential::amountToScalar(sendAmt);
        wit.randomness = rAmt;
        wit.balance = confidential::amountToScalar(bal);
        wit.balanceBlind = rho;
        wit.senderSk = aliceKp.sk;
        confidential::SendSigmaProof sigma{};
        BEAST_EXPECT(confidential::proveSendSigma(
            pub, wit, Slice(sendCtx.data(), sendCtx.size()), sigma));
        std::array<std::uint8_t, confidential::kAggregatedBulletproofBytes> bp{};
        BEAST_EXPECT(confidential::proveBulletproofAggregated(
            pcM, pcRem, sendAmt, bal - sendAmt, rAmt, remBlind, bp));
        std::vector<std::uint8_t> zk(confidential::kSendZkProofBytes);
        std::memcpy(zk.data(), sigma.data(), sigma.size());
        std::memcpy(zk.data() + sigma.size(), bp.data(), bp.size());

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTSend;
            jv[jss::Account] = alice.human();
            jv[jss::Destination] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfSenderEncryptedAmount] = hexCipher(encSender);
            jv[sfDestinationEncryptedAmount] = hexCipher(encDest);
            jv[sfIssuerEncryptedAmount] = hexCipher(encIss);
            jv[sfAmountCommitment] = hexPoint(pcM);
            jv[sfBalanceCommitment] = hexPoint(pcB);
            jv[sfZKProof] = strHex(Slice(zk.data(), zk.size()));
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        Scalar rBack = mustRandomScalar();
        auto backH = mustEncrypt(carolKp.pk, sendAmt, rBack);
        auto backI = mustEncrypt(issuer.pk, sendAmt, rBack);
        Scalar rhoB = mustRandomScalar();
        CompressedPoint pcBal{};
        BEAST_EXPECT(confidential::pedersenCommit(sendAmt, rhoB, pcBal));
        auto const sleCarol = env.le(keylet::mptoken(id, carol.id()));
        Ciphertext carolSpend{};
        BEAST_EXPECT(
            confidential::parseCiphertext((*sleCarol)[sfConfidentialBalanceSpending], carolSpend));
        confidential::ConvertBackSigmaPublicInput cbPub;
        cbPub.holderKey = carolKp.pk;
        cbPub.balanceC1 = carolSpend.c1;
        cbPub.balanceC2 = carolSpend.c2;
        cbPub.balanceCommitment = pcBal;
        auto const cbCtx = confidential::transactionContextIDConvertBack(
            static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT_BACK),
            Slice(carol.id().data(), carol.id().size()),
            Slice(id.data(), id.size()),
            env.seq(carol),
            (*sleCarol)[~sfConfidentialBalanceVersion].value_or(0));
        confidential::ConvertBackSigmaWitness cbWit;
        cbWit.balance = confidential::amountToScalar(sendAmt);
        cbWit.balanceBlind = rhoB;
        cbWit.holderSk = carolKp.sk;
        confidential::ConvertBackSigmaProof cbSigma{};
        BEAST_EXPECT(confidential::proveConvertBackSigma(
            cbPub, cbWit, Slice(cbCtx.data(), cbCtx.size()), cbSigma));
        CompressedPoint remC{};
        // remaining = 0 after converting the full spending balance
        BEAST_EXPECT(confidential::pedersenCommit(0, rhoB, remC));
        std::array<std::uint8_t, confidential::kSingleBulletproofBytes> cbBp{};
        BEAST_EXPECT(confidential::proveBulletproofSingle(remC, 0, rhoB, cbBp));
        std::vector<std::uint8_t> cbZk(confidential::kConvertBackZkProofBytes);
        std::memcpy(cbZk.data(), cbSigma.data(), cbSigma.size());
        std::memcpy(cbZk.data() + cbSigma.size(), cbBp.data(), cbBp.size());

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = std::to_string(sendAmt);
            jv[sfHolderEncryptedAmount] = hexCipher(backH);
            jv[sfIssuerEncryptedAmount] = hexCipher(backI);
            jv[sfBlindingFactor] = to_string(scalarToUint(rBack));
            jv[sfBalanceCommitment] = hexPoint(pcBal);
            jv[sfZKProof] = strHex(Slice(cbZk.data(), cbZk.size()));
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
        BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 60));
        {
            auto const sle = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(sle && (*sle)[sfConfidentialOutstandingAmount] == 30);
        }

        auto const sleAlice2 = env.le(keylet::mptoken(id, alice.id()));
        Ciphertext aliceIss{};
        BEAST_EXPECT(
            confidential::parseCiphertext((*sleAlice2)[sfIssuerEncryptedBalance], aliceIss));
        confidential::ClawbackSigmaPublicInput clPub;
        clPub.issuerKey = issuer.pk;
        clPub.issuerBalance = aliceIss;
        clPub.revealedAmount = 30;
        auto const clCtx = confidential::transactionContextIDClawback(
            static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CLAWBACK),
            Slice(gw.id().data(), gw.id().size()),
            Slice(id.data(), id.size()),
            env.seq(gw),
            Slice(alice.id().data(), alice.id().size()));
        confidential::ClawbackSigmaProof clProof{};
        BEAST_EXPECT(confidential::proveClawbackSigma(
            clPub, issuer.sk, Slice(clCtx.data(), clCtx.size()), clProof));
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "30";
            jv[sfZKProof] = hexOf(clProof);
            env(jv);
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
        {
            auto const sle = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(sle && (*sle)[sfConfidentialOutstandingAmount] == 0);
            BEAST_EXPECT((*sle)[sfOutstandingAmount] == 70);
        }
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 10));
    }

public:
    void
    run() override
    {
        testLifecycle(jtx::testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPT, app, xrpl);

}  // namespace xrpl::test
