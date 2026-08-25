#include <test/jtx.h>
#include <test/jtx/delegate.h>
#include <test/jtx/deposit.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/invariants/MPTInvariant.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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
        if (!confidential::pointMulBase(kp.sk, kp.pk))
            throw std::runtime_error("failed to derive test public key");
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
        if (!confidential::serializeCiphertext(
                ct, Slice(raw.data(), raw.size())))
            throw std::runtime_error("failed to serialize test ciphertext");
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
        if (!confidential::elgamalEncrypt(pk, amount, r, ct))
            throw std::runtime_error("failed to encrypt test amount");
        return ct;
    }


    static std::string
    badCipherHex()
    {
        std::string blob(confidential::kCiphertextBytes * 2, '0');
        blob[1] = '2';
        blob[67] = '2';
        return blob;
    }

    static std::string
    badPointHex()
    {
        std::string blob(confidential::kCompressedPointBytes * 2, '0');
        blob[1] = '2';
        return blob;
    }

    void
    testLifecycle(FeatureBitset features)
    {
        testcase("confidential convert merge send convert-back clawback");

        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const carol("carol");
        Account const aliceDelegate("aliceDelegate");
        Account const carolDelegate("carolDelegate");
        Env env(*this, features);
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
        env.fund(XRP(1000), aliceDelegate, carolDelegate);
        env(delegate::set(
            alice,
            aliceDelegate,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTSend"}));
        env(delegate::set(
            carol,
            carolDelegate,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTConvertBack"}));
        env(delegate::set(
            gw, carolDelegate, {"ConfidentialMPTClawback"}));
        env.close();

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
            auto const& delegatedSigner =
                acct == alice ? aliceDelegate : carolDelegate;
            jv[sfDelegate] = delegatedSigner.human();
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
            env(jv, delegate::As(delegatedSigner), Fee(XRP(1)));
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
            jv[sfDelegate] = aliceDelegate.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv, delegate::As(aliceDelegate), Fee(XRP(1)));
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
            env(bad, Ter(temBAD_AMOUNT), Fee(XRP(1)));
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
        BEAST_EXPECT(confidential::proveBulletproofSend(
            pcM, pcRem, sendAmt, bal - sendAmt, rAmt, remBlind, bp));
        std::vector<std::uint8_t> zk(confidential::kSendZkProofBytes);
        std::memcpy(zk.data(), sigma.data(), sigma.size());
        std::memcpy(zk.data() + sigma.size(), bp.data(), bp.size());

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTSend;
            jv[jss::Account] = alice.human();
            jv[sfDelegate] = aliceDelegate.human();
            jv[jss::Destination] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfSenderEncryptedAmount] = hexCipher(encSender);
            jv[sfDestinationEncryptedAmount] = hexCipher(encDest);
            jv[sfIssuerEncryptedAmount] = hexCipher(encIss);
            jv[sfAmountCommitment] = hexPoint(pcM);
            jv[sfBalanceCommitment] = hexPoint(pcB);
            jv[sfZKProof] = strHex(Slice(zk.data(), zk.size()));
            env(jv, delegate::As(aliceDelegate), Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = carol.human();
            jv[sfDelegate] = carolDelegate.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv, delegate::As(carolDelegate), Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        std::uint64_t const backAmt = 5;
        Scalar rBack = mustRandomScalar();
        auto backH = mustEncrypt(carolKp.pk, backAmt, rBack);
        auto backI = mustEncrypt(issuer.pk, backAmt, rBack);
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
        BEAST_EXPECT(confidential::pedersenCommit(sendAmt - backAmt, rhoB, remC));
        std::array<std::uint8_t, confidential::kSingleBulletproofBytes> cbBp{};
        BEAST_EXPECT(confidential::proveBulletproofSingle(remC, sendAmt - backAmt, rhoB, cbBp));
        std::vector<std::uint8_t> cbZk(confidential::kConvertBackZkProofBytes);
        std::memcpy(cbZk.data(), cbSigma.data(), cbSigma.size());
        std::memcpy(cbZk.data() + cbSigma.size(), cbBp.data(), cbBp.size());

        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = carol.human();
            jv[sfDelegate] = carolDelegate.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = std::to_string(backAmt);
            jv[sfHolderEncryptedAmount] = hexCipher(backH);
            jv[sfIssuerEncryptedAmount] = hexCipher(backI);
            jv[sfBlindingFactor] = to_string(scalarToUint(rBack));
            jv[sfBalanceCommitment] = hexPoint(pcBal);
            jv[sfZKProof] = strHex(Slice(cbZk.data(), cbZk.size()));
            env(jv, delegate::As(carolDelegate), Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
        BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 55));
        {
            auto const sle = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(sle && (*sle)[sfConfidentialOutstandingAmount] == 35);
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
            jv[sfDelegate] = carolDelegate.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "30";
            jv[sfZKProof] = hexOf(clProof);
            env(jv, delegate::As(carolDelegate), Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
        {
            auto const sle = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(sle && (*sle)[sfConfidentialOutstandingAmount] == 5);
            BEAST_EXPECT((*sle)[sfOutstandingAmount] == 70);
            BEAST_EXPECTS(
                (*sle)[sfConfidentialHolderCount] == 1,
                "clawback decrements confidential holder census");
        }
        BEAST_EXPECTS(
            !env.le(keylet::mptoken(id, alice.id()))
                 ->isFieldPresent(sfHolderEncryptionKey),
            "clawback clears confidential registration");
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 10));
        mpt.pay(alice, carol, 10);
        mpt.authorize({.account = alice, .flags = tfMPTUnauthorize});
        BEAST_EXPECTS(
            !env.le(keylet::mptoken(id, alice.id())),
            "zero-balance holder can delete confidential registration");
    }


    void
    testNegativePaths(FeatureBitset features)
    {
        testcase("confidential negative preflight/preclaim paths");
        using namespace jtx;
        Account const gw("gwN");
        Account const alice("aliceN");
        Account const carol("carolN");
        Account const eve("eveN");
        Env env(*this, features);
        env.fund(XRP(1000), eve);
        MPTTester mpt(env, gw, {.holders = {alice, carol}});
        mpt.create(
            {.pay = {{std::vector<Account>{alice, carol}, 80}},
             .flags = tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer | tfMPTCanClawback |
                 tfMPTCanLock});

        auto issuer = makeKey();
        auto aliceKp = makeKey();
        auto carolKp = makeKey();
        auto const id = mpt.issuanceID();

        auto encPair = [&](CompressedPoint const& hpk, std::uint64_t amt, Scalar const& r) {
            return std::pair{mustEncrypt(hpk, amt, r), mustEncrypt(issuer.pk, amt, r)};
        };

        // Convert: no issuer key yet
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecNO_PERMISSION), Fee(XRP(1)));
        }

        mpt.set(
            {.account = gw,
             .issuerEncryptionKey = std::string(
                 reinterpret_cast<char const*>(issuer.pk.data()), issuer.pk.size())});

        // Convert preflight malformations
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = std::to_string(kMaxMpTokenAmount + 1ull);
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temBAD_AMOUNT), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = std::string(64, '0');
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));  // missing proof
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            jv[sfZKProof] = std::string(8, '0');
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);  // proof without key
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = badCipherHex();
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temBAD_CIPHERTEXT), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfAuditorEncryptedAmount] = badCipherHex();
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temBAD_CIPHERTEXT), Fee(XRP(1)));
        }

        // Convert preclaim
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(uint192{});
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = gw.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = eve.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1000";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecINSUFFICIENT_FUNDS), Fee(XRP(1)));
        }

        // Successful init
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 20, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "20";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            auto const ctxId = confidential::transactionContextIDConvert(
                static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                Slice(alice.id().data(), alice.id().size()),
                Slice(id.data(), id.size()),
                env.seq(alice));
            confidential::SchnorrRegisterProof proof{};
            BEAST_EXPECT(confidential::proveSchnorrRegister(
                aliceKp.sk, aliceKp.pk, Slice(ctxId.data(), ctxId.size()), proof));
            jv[sfZKProof] = hexOf(proof);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(carolKp.pk, 0, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "0";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(carolKp.pk);
            auto const ctxId = confidential::transactionContextIDConvert(
                static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                Slice(carol.id().data(), carol.id().size()),
                Slice(id.data(), id.size()),
                env.seq(carol));
            confidential::SchnorrRegisterProof proof{};
            BEAST_EXPECT(confidential::proveSchnorrRegister(
                carolKp.sk, carolKp.pk, Slice(ctxId.data(), ctxId.size()), proof));
            jv[sfZKProof] = hexOf(proof);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        // Duplicate key registration
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecDUPLICATE), Fee(XRP(1)));
        }

        // Bad proofs / mismatched ciphertexts
        {
            Scalar r = mustRandomScalar();
            auto [h, i] = encPair(aliceKp.pk, 1, r);
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(h);
            jv[sfIssuerEncryptedAmount] = hexCipher(i);
            jv[sfBlindingFactor] = to_string(uint256{});
            env(jv, Ter(tecBAD_PROOF), Fee(XRP(1)));
        }
        {
            Scalar r1 = mustRandomScalar();
            Scalar r2 = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r1));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r2));
            jv[sfBlindingFactor] = to_string(scalarToUint(r1));
            env(jv, Ter(tecBAD_PROOF), Fee(XRP(1)));
        }

        // Issuance freeze coverage exercised in testFreezePaths (separate Env).

        // Merge errors
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(uint192{});
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = gw.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = eve.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        // Clawback errors
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = gw.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "0";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temBAD_AMOUNT), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfZKProof] = std::string(8, '0');
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(uint192{});
            jv[sfMPTAmount] = "1";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = alice.human();
            jv[sfHolder] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = eve.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1000";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecINSUFFICIENT_FUNDS), Fee(XRP(1)));
        }
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            std::array<std::uint8_t, confidential::kClawbackSigmaProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecBAD_PROOF), Fee(XRP(1)));
        }

        // Send / ConvertBack preflight shells
        auto sendShell = [&]() {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTSend;
            jv[jss::Account] = alice.human();
            jv[jss::Destination] = carol.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfSenderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 0, r));
            jv[sfDestinationEncryptedAmount] = hexCipher(mustEncrypt(carolKp.pk, 0, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 0, r));
            jv[sfAmountCommitment] = hexPoint(aliceKp.pk);
            jv[sfBalanceCommitment] = hexPoint(carolKp.pk);
            std::array<std::uint8_t, confidential::kSendZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            return jv;
        };
        {
            auto jv = sendShell();
            jv[jss::Destination] = alice.human();
            env(jv, Ter(temREDUNDANT), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[sfSenderEncryptedAmount] = badCipherHex();
            env(jv, Ter(temBAD_CIPHERTEXT), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[sfAmountCommitment] = badPointHex();
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[sfZKProof] = std::string(8, '0');
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[sfMPTokenIssuanceID] = to_string(uint192{});
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[jss::Account] = gw.human();
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            auto jv = sendShell();
            jv[jss::Destination] = eve.human();
            // eve exists but may lack dest mpt — also account exists
            // Use unknown account name for tecNO_DST
            Account const ghost("ghostN");
            jv[jss::Destination] = ghost.human();
            env(jv, Ter(tecNO_DST), Fee(XRP(1)));
        }
        env(fset(carol, asfRequireDest));
        {
            auto jv = sendShell();
            env(jv, Ter(tecDST_TAG_NEEDED), Fee(XRP(1)));
        }
        env(fclear(carol, asfRequireDest));

        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = badCipherHex();
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(temBAD_CIPHERTEXT), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = badPointHex();
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            jv[sfZKProof] = std::string(8, '0');
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(uint192{});
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(tecOBJECT_NOT_FOUND), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = gw.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(temMALFORMED), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1000";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1000, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1000, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(tecINSUFFICIENT_FUNDS), Fee(XRP(1)));
        }
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "1";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 1, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 1, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfBalanceCommitment] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kConvertBackZkProofBytes> zk{};
            jv[sfZKProof] = hexOf(zk);
            env(jv, Ter(tecBAD_PROOF), Fee(XRP(1)));
        }

        // Deposit auth blocks send before proof
        env(fset(carol, asfDepositAuth));
        {
            auto jv = sendShell();
            env(jv, Ter(tecNO_PERMISSION), Fee(XRP(1)));
        }
    }

    void
    testAuditorAndLedger(FeatureBitset features)
    {
        testcase("auditor convert/clawback");
        using namespace jtx;
        Account const gw("gwAud");
        Account const alice("aliceAud");
        Env env(*this, features);
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create(
            {.pay = {{std::vector<Account>{alice}, 50}},
             .flags = tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer | tfMPTCanClawback});

        auto issuer = makeKey();
        auto auditor = makeKey();
        auto aliceKp = makeKey();
        auto const id = mpt.issuanceID();
        mpt.set(
            {.account = gw,
             .issuerEncryptionKey = std::string(
                 reinterpret_cast<char const*>(issuer.pk.data()), issuer.pk.size()),
             .auditorEncryptionKey = std::string(
                 reinterpret_cast<char const*>(auditor.pk.data()), auditor.pk.size())});

        // Missing auditor amount
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "10";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 10, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 10, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            std::array<std::uint8_t, confidential::kSchnorrRegisterProofBytes> proof{};
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecNO_PERMISSION), Fee(XRP(1)));
        }

        // Bad auditor plaintext
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "10";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 10, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 10, r));
            jv[sfAuditorEncryptedAmount] = hexCipher(mustEncrypt(auditor.pk, 9, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            auto const ctxId = confidential::transactionContextIDConvert(
                static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                Slice(alice.id().data(), alice.id().size()),
                Slice(id.data(), id.size()),
                env.seq(alice));
            confidential::SchnorrRegisterProof proof{};
            BEAST_EXPECT(confidential::proveSchnorrRegister(
                aliceKp.sk, aliceKp.pk, Slice(ctxId.data(), ctxId.size()), proof));
            jv[sfZKProof] = hexOf(proof);
            env(jv, Ter(tecBAD_PROOF), Fee(XRP(1)));
        }

        // Success with auditor
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "25";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 25, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 25, r));
            jv[sfAuditorEncryptedAmount] = hexCipher(mustEncrypt(auditor.pk, 25, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(aliceKp.pk);
            auto const ctxId = confidential::transactionContextIDConvert(
                static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                Slice(alice.id().data(), alice.id().size()),
                Slice(id.data(), id.size()),
                env.seq(alice));
            confidential::SchnorrRegisterProof proof{};
            BEAST_EXPECT(confidential::proveSchnorrRegister(
                aliceKp.sk, aliceKp.pk, Slice(ctxId.data(), ctxId.size()), proof));
            jv[sfZKProof] = hexOf(proof);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
            BEAST_EXPECT(env.le(keylet::mptoken(id, alice.id()))
                             ->isFieldPresent(sfAuditorEncryptedBalance));
        }

        // Second convert updates auditor balance
        {
            Scalar r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "5";
            jv[sfHolderEncryptedAmount] = hexCipher(mustEncrypt(aliceKp.pk, 5, r));
            jv[sfIssuerEncryptedAmount] = hexCipher(mustEncrypt(issuer.pk, 5, r));
            jv[sfAuditorEncryptedAmount] = hexCipher(mustEncrypt(auditor.pk, 5, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }

        // Clawback with auditor reset
        {
            auto const sleAlice = env.le(keylet::mptoken(id, alice.id()));
            Ciphertext aliceIss{};
            BEAST_EXPECT(confidential::parseCiphertext(
                (*sleAlice)[sfIssuerEncryptedBalance], aliceIss));
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
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[jss::Account] = gw.human();
            jv[sfHolder] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = "30";
            jv[sfZKProof] = hexOf(clProof);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
    }

    void
    testAuditorRotation(FeatureBitset features)
    {
        testcase("frozen two-phase auditor rotation");
        using namespace jtx;
        Account const gw("gwRotate");
        Account const alice("aliceRotate");
        Account const bob("bobRotate");
        Account const auditorDelegate("auditorDelegate");
        Env env(*this, features);
        MPTTester mpt(env, gw, {.holders = {alice, bob}});
        mpt.create(
            {.pay = {{std::vector<Account>{alice, bob}, 50}},
             .flags = tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer});

        auto const issuer = makeKey();
        auto const oldAuditor = makeKey();
        auto const newAuditor = makeKey();
        auto const aliceKey = makeKey();
        auto const bobKey = makeKey();
        auto const id = mpt.issuanceID();
        auto rawKey = [](CompressedPoint const& key) {
            return std::string(
                reinterpret_cast<char const*>(key.data()), key.size());
        };
        mpt.set(
            {.account = gw,
             .issuerEncryptionKey = rawKey(issuer.pk),
             .auditorEncryptionKey = rawKey(oldAuditor.pk)});
        env.fund(XRP(1000), auditorDelegate);
        env(delegate::set(
            gw,
            auditorDelegate,
            {"ConfidentialMPTReencryptAuditor"}));
        env.close();

        auto convert = [&](Account const& holder,
                           Keypair const& holderKey,
                           std::uint64_t amount) {
            auto const r = mustRandomScalar();
            json::Value jv;
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[jss::Account] = holder.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfMPTAmount] = std::to_string(amount);
            jv[sfHolderEncryptedAmount] =
                hexCipher(mustEncrypt(holderKey.pk, amount, r));
            jv[sfIssuerEncryptedAmount] =
                hexCipher(mustEncrypt(issuer.pk, amount, r));
            jv[sfAuditorEncryptedAmount] =
                hexCipher(mustEncrypt(oldAuditor.pk, amount, r));
            jv[sfBlindingFactor] = to_string(scalarToUint(r));
            jv[sfHolderEncryptionKey] = hexPoint(holderKey.pk);
            auto const context = confidential::transactionContextIDConvert(
                static_cast<std::uint16_t>(ttCONFIDENTIAL_MPT_CONVERT),
                Slice(holder.id().data(), holder.id().size()),
                Slice(id.data(), id.size()),
                env.seq(holder));
            confidential::SchnorrRegisterProof proof{};
            BEAST_EXPECT(confidential::proveSchnorrRegister(
                holderKey.sk,
                holderKey.pk,
                Slice(context.data(), context.size()),
                proof));
            jv[sfZKProof] = hexOf(proof);
            env(jv, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        };
        convert(alice, aliceKey, 10);
        convert(bob, bobKey, 20);
        env.close();

        {
            auto const issuance = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT((*issuance)[sfConfidentialHolderCount] == 2);
            BEAST_EXPECT((*issuance)[sfAuditorKeyVersion] == 1);
        }

        mpt.set(
            {.account = gw,
             .auditorEncryptionKey = rawKey(newAuditor.pk)});
        {
            auto const issuance = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(
                issuance->isFieldPresent(sfPendingAuditorEncryptionKey));
            BEAST_EXPECT((*issuance)[sfAuditorMigrationCount] == 2);
            BEAST_EXPECT((*issuance)[sfAuditorKeyVersion] == 1);
        }

        {
            json::Value merge;
            merge[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            merge[jss::Account] = alice.human();
            merge[sfMPTokenIssuanceID] = to_string(id);
            env(merge, Ter(tecLOCKED), Fee(XRP(1)));
        }

        auto migrate = [&](Account const& holder,
                           std::uint64_t amount,
                           bool tamper) {
            auto const mptSle = env.le(keylet::mptoken(id, holder.id()));
            Ciphertext issuerBalance{};
            BEAST_EXPECT(confidential::parseCiphertext(
                (*mptSle)[sfIssuerEncryptedBalance], issuerBalance));

            auto const r = mustRandomScalar();
            auto const auditorBalance =
                mustEncrypt(newAuditor.pk, amount, r);
            confidential::AuditorEqualitySigmaPublicInput pub{
                issuer.pk,
                issuerBalance,
                newAuditor.pk,
                auditorBalance};
            confidential::AuditorEqualitySigmaWitness witness{
                issuer.sk,
                r,
                confidential::amountToScalar(amount)};
            auto const context =
                confidential::transactionContextIDMigrateAuditor(
                    static_cast<std::uint16_t>(
                        ttCONFIDENTIAL_MPT_REENCRYPT_AUDITOR),
                    Slice(gw.id().data(), gw.id().size()),
                    Slice(id.data(), id.size()),
                    env.seq(gw),
                    Slice(holder.id().data(), holder.id().size()),
                    2);
            confidential::AuditorEqualitySigmaProof proof{};
            BEAST_EXPECT(confidential::proveAuditorEqualitySigma(
                pub,
                witness,
                Slice(context.data(), context.size()),
                proof));
            if (tamper)
                proof.back() ^= 1;

            json::Value jv;
            jv[jss::TransactionType] =
                jss::ConfidentialMPTReencryptAuditor;
            jv[jss::Account] = gw.human();
            jv[sfDelegate] = auditorDelegate.human();
            jv[sfHolder] = holder.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfAuditorEncryptedBalance] = hexCipher(auditorBalance);
            jv[sfZKProof] = hexOf(proof);
            env(
                jv,
                tamper ? Ter(tecBAD_PROOF) : Ter(tesSUCCESS),
                delegate::As(auditorDelegate),
                Fee(XRP(1)));
        };

        migrate(alice, 10, true);
        migrate(alice, 10, false);
        {
            auto const issuance = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT((*issuance)[sfAuditorMigrationCount] == 1);
            BEAST_EXPECT(
                env.le(keylet::mptoken(id, alice.id()))
                    ->getFieldU32(sfAuditorKeyVersion) == 2);
        }
        migrate(bob, 20, false);
        {
            auto const issuance = env.le(keylet::mptIssuance(id));
            BEAST_EXPECT(
                !issuance->isFieldPresent(sfPendingAuditorEncryptionKey));
            BEAST_EXPECT(
                !issuance->isFieldPresent(sfAuditorMigrationCount));
            BEAST_EXPECT((*issuance)[sfAuditorKeyVersion] == 2);
            BEAST_EXPECT(
                (*issuance)[sfAuditorEncryptionKey] ==
                Slice(newAuditor.pk.data(), newAuditor.pk.size()));
        }

        {
            json::Value merge;
            merge[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
            merge[jss::Account] = alice.human();
            merge[sfMPTokenIssuanceID] = to_string(id);
            env(merge, Fee(XRP(1)));
            BEAST_EXPECT(env.ter() == tesSUCCESS);
        }
    }

    void
    testConfidentialInvariants(FeatureBitset features)
    {
        testcase("ValidConfidentialMPT invariant failures");
        using namespace jtx;
        Account const a1{"invA1"};
        Account const a2{"invA2"};

        auto runInvariant = [&](std::string const& expectLog, auto const& precheck) {
            Env env(*this, features);
            env.fund(XRP(1000), a1, a2);
            env.close();
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            STTx tx{ttACCOUNT_SET, [](STObject&) {}};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());
            BEAST_EXPECT(precheck(a1, a2, ac));
            ValidConfidentialMPT confidential;
            ValidMPTPayment payment;
            ac.visit([&](
                         uint256 const&,
                         bool isDelete,
                         std::shared_ptr<SLE const> const& before,
                         std::shared_ptr<SLE const> const& after) {
                confidential.visitEntry(isDelete, before, after);
                payment.visitEntry(isDelete, before, after);
            });
            bool const passes =
                expectLog == "invalid OutstandingAmount balance"
                ? payment.finalize(
                      tx, tesSUCCESS, XRPAmount{}, ac.view(), jlog)
                : confidential.finalize(
                      tx, tesSUCCESS, XRPAmount{}, ac.view(), jlog);
            BEAST_EXPECT(!passes);
            BEAST_EXPECTS(
                sink.messages().str().find(expectLog) != std::string::npos,
                sink.messages().str());
        };

        runInvariant("invalid confidential MPT state", [](Account const& a1, Account const&, ApplyContext& ac) {
            auto const sle = ac.view().peek(keylet::account(a1.id()));
            if (!sle)
                return false;
            MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
            auto iss = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
            iss->setAccountID(sfIssuer, a1.id());
            iss->setFieldU32(sfSequence, sle->getFieldU32(sfSequence));
            iss->setFieldU64(sfOutstandingAmount, 5);
            iss->setFieldU64(sfConfidentialOutstandingAmount, 9);
            iss->setFieldU64(sfMaximumAmount, 100);
            ac.view().insert(iss);
            return true;
        });

        runInvariant("invalid confidential MPT state", [](Account const& a1, Account const& a2, ApplyContext& ac) {
            auto const sle = ac.view().peek(keylet::account(a1.id()));
            if (!sle)
                return false;
            MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
            auto iss = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
            iss->setAccountID(sfIssuer, a1.id());
            iss->setFieldU32(sfSequence, sle->getFieldU32(sfSequence));
            iss->setFieldU64(sfOutstandingAmount, 10);
            iss->setFieldU64(sfMaximumAmount, 100);
            iss->setFieldU32(sfFlags, lsfMPTCanHoldConfidentialBalance);
            ac.view().insert(iss);
            auto tok = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
            tok->setAccountID(sfAccount, a2.id());
            tok->setFieldH192(sfMPTokenIssuanceID, mpt.getMptID());
            tok->setFieldU64(sfMPTAmount, 10);
            std::vector<std::uint8_t> key(33, 0x02);
            tok->setFieldVL(sfHolderEncryptionKey, Slice(key.data(), key.size()));
            ac.view().insert(tok);
            return true;
        });

        runInvariant("confidential balance without enabled issuance", [](Account const& a1, Account const& a2, ApplyContext& ac) {
            auto const sle = ac.view().peek(keylet::account(a1.id()));
            if (!sle)
                return false;
            MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
            auto iss = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
            iss->setAccountID(sfIssuer, a1.id());
            iss->setFieldU32(sfSequence, sle->getFieldU32(sfSequence));
            iss->setFieldU64(sfOutstandingAmount, 10);
            iss->setFieldU64(sfMaximumAmount, 100);
            ac.view().insert(iss);
            std::vector<std::uint8_t> key(33, 0x02);
            std::vector<std::uint8_t> ct(66, 0x02);
            auto tok = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
            tok->setAccountID(sfAccount, a2.id());
            tok->setFieldH192(sfMPTokenIssuanceID, mpt.getMptID());
            tok->setFieldU64(sfMPTAmount, 10);
            tok->setFieldVL(sfHolderEncryptionKey, Slice(key.data(), key.size()));
            tok->setFieldVL(sfConfidentialBalanceSpending, Slice(ct.data(), ct.size()));
            tok->setFieldVL(sfConfidentialBalanceInbox, Slice(ct.data(), ct.size()));
            tok->setFieldVL(sfIssuerEncryptedBalance, Slice(ct.data(), ct.size()));
            tok->setFieldU32(sfConfidentialBalanceVersion, 0);
            ac.view().insert(tok);
            return true;
        });

        runInvariant("invalid OutstandingAmount balance", [](Account const& a1, Account const& a2, ApplyContext& ac) {
            auto const sle = ac.view().peek(keylet::account(a1.id()));
            if (!sle)
                return false;
            MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
            auto iss = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
            iss->setAccountID(sfIssuer, a1.id());
            iss->setFieldU32(sfSequence, sle->getFieldU32(sfSequence));
            iss->setFieldU64(sfOutstandingAmount, 100);
            iss->setFieldU64(sfConfidentialOutstandingAmount, 20);
            iss->setFieldU64(sfMaximumAmount, 100);
            ac.view().insert(iss);
            auto tok = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
            tok->setAccountID(sfAccount, a2.id());
            tok->setFieldH192(sfMPTokenIssuanceID, mpt.getMptID());
            tok->setFieldU64(sfMPTAmount, 50);
            ac.view().insert(tok);
            return true;
        });

        {
            Env env(*this, features);
            env.fund(XRP(1000), a1, a2);
            env.close();
            OpenView view{*env.current()};
            auto const account = view.read(keylet::account(a1.id()));
            MPTIssue const mpt{
                makeMptID(account->getFieldU32(sfSequence), a1)};
            auto issuance =
                std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
            issuance->setAccountID(sfIssuer, a1.id());
            issuance->setFieldU32(
                sfSequence, account->getFieldU32(sfSequence));
            issuance->setFieldU64(sfOutstandingAmount, 10);
            issuance->setFieldU64(sfMaximumAmount, 100);
            issuance->setFieldU32(
                sfFlags, lsfMPTCanHoldConfidentialBalance);
            view.rawInsert(issuance);

            std::vector<std::uint8_t> key(33, 0x02);
            std::vector<std::uint8_t> ct(66, 0x02);
            auto before =
                std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
            before->setAccountID(sfAccount, a2.id());
            before->setFieldH192(sfMPTokenIssuanceID, mpt.getMptID());
            before->setFieldU64(sfMPTAmount, 10);
            before->setFieldVL(
                sfHolderEncryptionKey, Slice(key.data(), key.size()));
            before->setFieldVL(
                sfConfidentialBalanceSpending,
                Slice(ct.data(), ct.size()));
            before->setFieldVL(
                sfConfidentialBalanceInbox, Slice(ct.data(), ct.size()));
            before->setFieldVL(
                sfIssuerEncryptedBalance, Slice(ct.data(), ct.size()));
            before->setFieldU32(sfConfidentialBalanceVersion, 3);
            auto after = std::make_shared<SLE>(*before);
            std::vector<std::uint8_t> ct2(66, 0x03);
            after->setFieldVL(
                sfConfidentialBalanceSpending,
                Slice(ct2.data(), ct2.size()));

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ValidConfidentialMPT checker;
            checker.visitEntry(false, before, after);
            STTx tx{ttACCOUNT_SET, [](STObject&) {}};
            BEAST_EXPECT(!checker.finalize(
                tx, tesSUCCESS, XRPAmount{}, view, jlog));
            BEAST_EXPECT(
                sink.messages().str().find(
                    "invalid confidential MPT state") !=
                std::string::npos);
        }
    }

public:
    void
    run() override
    {
        auto const all = jtx::testableAmendments();
        testLifecycle(all);
        testNegativePaths(all);
        testAuditorAndLedger(all);
        testAuditorRotation(all);
        testConfidentialInvariants(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPT, app, xrpl);

}  // namespace xrpl::test
