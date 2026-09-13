#include <test/jtx/Account.h>
#include <test/jtx/Ballot.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <chrono>
#include <sstream>
#include <string>

namespace xrpl {
namespace test {

using namespace jtx;
using namespace std::chrono_literals;

class ConfidentialVoting_test : public beast::unit_test::Suite
{
    // Build a BallotCreate transaction (token mode) as raw JSON.
    static json::Value
    createTokenBallot(
        Account const& owner,
        uint192 const& issuanceID,
        ballot::Keypair const& tally,
        std::uint8_t optionCount,
        std::uint32_t openTime,
        std::uint32_t closeTime,
        uint256 const& ballotID)
    {
        json::Value jv;
        jv[jss::TransactionType] = "BallotCreate";
        jv[jss::Account] = owner.human();
        jv[sfMPTokenIssuanceID.jsonName] = to_string(issuanceID);
        jv[sfDigest.jsonName] = std::string(64, 'A');
        jv[sfOptionCount.jsonName] = optionCount;
        jv[sfTallyPublicKey.jsonName] = strHex(tally.pub);
        jv[sfOpenTime.jsonName] = openTime;
        jv[sfCloseTime.jsonName] = closeTime;
        jv[sfZKProof.jsonName] = strHex(ballot::schnorrProof(tally, ballotID));
        jv[jss::Fee] = "1000";  // covers the confidential fee multiplier
        return jv;
    }

    // Build a BallotCastVote transaction as raw JSON.
    static json::Value
    castVote(Account const& voter, uint256 const& ballotID, BallotCast const& cast)
    {
        json::Value jv;
        jv[jss::TransactionType] = "BallotCastVote";
        jv[jss::Account] = voter.human();
        jv[sfBallotID.jsonName] = to_string(ballotID);
        jv[sfEncryptedVotes.jsonName] =
            ballotOptionArray(cast.ciphertexts, &cast.commitments, &cast.linkageProofs);
        jv[sfZKProof.jsonName] = strHex(cast.proof);
        jv[sfBlindingFactor.jsonName] = strHex(cast.blinding);
        jv[jss::Fee] = "1000";  // covers the confidential fee multiplier
        return jv;
    }

    // Build a BallotFinalize transaction as raw JSON.
    static json::Value
    finalize(
        Account const& authority,
        uint256 const& ballotID,
        std::vector<std::uint64_t> const& results,
        Buffer const& proof)
    {
        json::Value jv;
        jv[jss::TransactionType] = "BallotFinalize";
        jv[jss::Account] = authority.human();
        jv[sfBallotID.jsonName] = to_string(ballotID);
        json::Value arr(json::ValueType::Array);
        for (auto const r : results)
        {
            json::Value inner;
            // sfBallotWeight is a UINT64; its JSON string form is parsed as hex.
            std::stringstream ss;
            ss << std::hex << r;
            inner[sfBallotWeight.jsonName] = ss.str();
            json::Value elem;
            elem[sfBallotResult.jsonName] = inner;
            arr.append(elem);
        }
        jv[sfResults.jsonName] = arr;
        jv[sfZKProof.jsonName] = strHex(proof);
        jv[jss::Fee] = "1000";  // covers the confidential fee multiplier
        return jv;
    }

    static json::Value
    deleteBallot(Account const& account, uint256 const& ballotID)
    {
        json::Value jv;
        jv[jss::TransactionType] = "BallotDelete";
        jv[jss::Account] = account.human();
        jv[sfBallotID.jsonName] = to_string(ballotID);
        return jv;
    }

    void
    testAmendmentDisabled()
    {
        testcase("Amendment disabled");
        Env env{*this, testableAmendments() - featureConfidentialVoting};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        MPTTester mpt(env, alice, {.fund = false});
        mpt.create({.flags = tfMPTCanTransfer});

        auto const tally = ballot::generateKeypair();
        auto const seq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), seq).key;
        auto jv = createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, 0xFFFFFFFFu, ballotID);
        env(jv, Ter(temDISABLED));
    }

    void
    testTokenHappyPath()
    {
        testcase("Token mode happy path");
        Env env{*this};
        Account const alice{"alice"};  // issuer + tally authority
        Account const bob{"bob"};      // voter
        env.fund(XRP(10000), alice, bob);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();

        // Create with a finite voting window.
        auto const nowSecs = env.now().time_since_epoch().count();
        auto const createSeq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), createSeq).key;
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, nowSecs + 1000, ballotID));
        env.close();

        // Cast: bob assigns all 100 weight to option 0.
        auto const castSeq = env.seq(bob);
        auto const castCtx = getBallotCastContextHash(bob.id(), ballotID, castSeq);
        auto const cast = makeBallotCast(tally, 2, /*choice=*/0, /*weight=*/100, castCtx);
        env(castVote(bob, ballotID, cast));
        env.close();

        // VoteCount incremented and lock recorded.
        {
            auto const sle = env.le(keylet::ballot(ballotID));
            BEAST_EXPECT(sle && (*sle)[sfVoteCount] == 1);
            auto const mptSle = env.le(keylet::mptoken(mpt.issuanceID(), bob.id()));
            BEAST_EXPECT(mptSle && (*mptSle)[~sfVoteLockedAmount].value_or(0) == 100);
        }

        // Close the voting window.
        env.close(2000s);

        // Finalize: read the on-ledger tally and prove its decryption.
        auto const sle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(sle);
        auto const& tallyArr = sle->getFieldArray(sfEncryptedTally);
        std::vector<std::uint64_t> results;
        std::vector<Buffer> proofBufs;
        auto const finSeq = env.seq(alice);
        auto const finCtx = getBallotFinalizeContextHash(alice.id(), ballotID, finSeq);
        for (auto const& entry : tallyArr)
        {
            Slice const ct = entry[sfEncryptedVote];
            auto const amt = ballot::decrypt(ct, tally.priv, 1000);
            BEAST_EXPECT(amt.has_value());
            results.push_back(*amt);
            proofBufs.push_back(ballot::decryptionProof(tally, *amt, ct, finCtx));
        }
        BEAST_EXPECT(results.size() == 2 && results[0] == 100 && results[1] == 0);

        Buffer proof(proofBufs.size() * kEcClawbackProofLength);
        for (std::size_t i = 0; i < proofBufs.size(); ++i)
            std::memcpy(
                proof.data() + (i * kEcClawbackProofLength),
                proofBufs[i].data(),
                kEcClawbackProofLength);

        env(finalize(alice, ballotID, results, proof));
        env.close();

        {
            auto const finSle = env.le(keylet::ballot(ballotID));
            BEAST_EXPECT(finSle && finSle->isFlag(lsfBallotFinalized));
            auto const& res = finSle->getFieldArray(sfResults);
            BEAST_EXPECT(
                res.size() == 2 && res[0][sfBallotWeight] == 100 && res[1][sfBallotWeight] == 0);
        }

        // Lock released after close: bob can now transfer.
        mpt.pay(bob, alice, 50);

        // Creator reclaims the ballot reserve.
        env(deleteBallot(alice, ballotID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ballot(ballotID)));
    }

    void
    testCastFailures()
    {
        testcase("Cast failures");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};  // no MPToken
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();

        // A ballot that is not yet open. Uses a separate issuance so it does not
        // collide with the open ballot below (one open ballot per issuance).
        MPTTester mpt2(env, alice, {.holders = {bob}, .fund = false});
        mpt2.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt2.authorize({.account = bob});
        mpt2.pay(alice, bob, 100);
        env.close();

        auto const nowSecs = env.now().time_since_epoch().count();
        auto const futureSeq = env.seq(alice);
        auto const futureBallot = keylet::ballot(alice.id(), futureSeq).key;
        env(createTokenBallot(
            alice, mpt2.issuanceID(), tally, 2, nowSecs + 10000, nowSecs + 20000, futureBallot));
        env.close();
        {
            auto const castSeq = env.seq(bob);
            auto const cast = makeBallotCast(
                tally, 2, 0, 100, getBallotCastContextHash(bob.id(), futureBallot, castSeq));
            env(castVote(bob, futureBallot, cast), Ter(tecBALLOT_NOT_OPEN));
            env.close();
        }

        // An open ballot for the remaining cases.
        auto const openSeq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), openSeq).key;
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, nowSecs + 5000, ballotID));
        env.close();

        // Malformed proof: corrupt the range proof.
        {
            auto const castSeq = env.seq(bob);
            auto cast = makeBallotCast(
                tally, 2, 0, 100, getBallotCastContextHash(bob.id(), ballotID, castSeq));
            cast.proof.data()[0] ^= 0xFF;
            env(castVote(bob, ballotID, cast), Ter(tecBAD_PROOF));
            env.close();
        }

        // Ineligible: carol holds no MPToken.
        {
            auto const castSeq = env.seq(carol);
            auto const cast = makeBallotCast(
                tally, 2, 0, 1, getBallotCastContextHash(carol.id(), ballotID, castSeq));
            env(castVote(carol, ballotID, cast), Ter(tecNO_ENTRY));
            env.close();
        }

        // Happy cast, then a double-cast is rejected.
        {
            auto const castSeq = env.seq(bob);
            auto const cast = makeBallotCast(
                tally, 2, 0, 100, getBallotCastContextHash(bob.id(), ballotID, castSeq));
            env(castVote(bob, ballotID, cast));
            env.close();

            auto const castSeq2 = env.seq(bob);
            auto const cast2 = makeBallotCast(
                tally, 2, 0, 100, getBallotCastContextHash(bob.id(), ballotID, castSeq2));
            env(castVote(bob, ballotID, cast2), Ter(tecBALLOT_VOTED));
            env.close();
        }

        // Cast after close is rejected.
        env.close(6000s);
        {
            Account const dave{"dave"};
            env.fund(XRP(10000), dave);
            mpt.authorize({.account = dave});
            mpt.pay(alice, dave, 10);
            auto const castSeq = env.seq(dave);
            auto const cast = makeBallotCast(
                tally, 2, 0, 10, getBallotCastContextHash(dave.id(), ballotID, castSeq));
            env(castVote(dave, ballotID, cast), Ter(tecBALLOT_CLOSED));
            env.close();
        }
    }

    void
    testFinalizeFailures()
    {
        testcase("Finalize failures");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const nowSecs = env.now().time_since_epoch().count();
        auto const createSeq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), createSeq).key;
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, nowSecs + 3000, ballotID));
        env.close();

        auto const castSeq = env.seq(bob);
        auto const cast =
            makeBallotCast(tally, 2, 0, 100, getBallotCastContextHash(bob.id(), ballotID, castSeq));
        env(castVote(bob, ballotID, cast));
        env.close();

        // Finalize before close is rejected.
        {
            auto const sle = env.le(keylet::ballot(ballotID));
            auto const& tallyArr = sle->getFieldArray(sfEncryptedTally);
            auto const finSeq = env.seq(alice);
            auto const finCtx = getBallotFinalizeContextHash(alice.id(), ballotID, finSeq);
            std::vector<std::uint64_t> results{100, 0};
            Buffer proof(2 * kEcClawbackProofLength);
            for (std::size_t i = 0; i < 2; ++i)
            {
                auto p = ballot::decryptionProof(
                    tally, results[i], tallyArr[i][sfEncryptedVote], finCtx);
                std::memcpy(
                    proof.data() + (i * kEcClawbackProofLength), p.data(), kEcClawbackProofLength);
            }
            env(finalize(alice, ballotID, results, proof), Ter(tecTOO_SOON));
            env.close();
        }

        // Close the window.
        env.close(4000s);

        // Finalize with wrong results is rejected: proofs are generated for the
        // true counts (100, 0), but the submitted results claim (99, 0), so the
        // decryption-correctness check fails.
        {
            auto const sle = env.le(keylet::ballot(ballotID));
            auto const& tallyArr = sle->getFieldArray(sfEncryptedTally);
            auto const finSeq = env.seq(alice);
            auto const finCtx = getBallotFinalizeContextHash(alice.id(), ballotID, finSeq);
            std::vector<std::uint64_t> const trueResults{100, 0};
            Buffer proof(2 * kEcClawbackProofLength);
            for (std::size_t i = 0; i < 2; ++i)
            {
                auto p = ballot::decryptionProof(
                    tally, trueResults[i], tallyArr[i][sfEncryptedVote], finCtx);
                std::memcpy(
                    proof.data() + (i * kEcClawbackProofLength), p.data(), kEcClawbackProofLength);
            }
            std::vector<std::uint64_t> const wrong{99, 0};
            env(finalize(alice, ballotID, wrong, proof), Ter(tecBAD_PROOF));
            env.close();
        }
    }

    // Low-level check that the compact_standard vote-linkage proof (with the
    // canonical vacuous balance witness) verifies, and rejects a commitment to a
    // different value than the ciphertext.
    void
    testLinkageRoundTrip()
    {
        testcase("Vote linkage compact_standard round-trip");
        auto* const ctx = mpt_secp256k1_context();

        auto const tally = ballot::generateKeypair();
        std::uint64_t const m = 42;
        auto const r = ballot::randomScalar();

        // ct = Enc(m; r) under the tally key; split into C1, C2.
        auto const ctBuf = ballot::encrypt(m, tally.pub, r);
        auto const ct = makeEcPair(ctBuf);
        BEAST_EXPECT(ct.has_value());

        secp256k1_pubkey tallyPub;
        BEAST_EXPECT(
            secp256k1_ec_pubkey_parse(ctx, &tallyPub, tally.pub.data(), tally.pub.size()) == 1);

        // PC_m = m*G + r*H (blinded by the ElGamal randomness r).
        auto const pcmBuf = ballot::pedersen(m, r);
        secp256k1_pubkey pcm;
        BEAST_EXPECT(secp256k1_ec_pubkey_parse(ctx, &pcm, pcmBuf.data(), pcmBuf.size()) == 1);

        // Vacuous balance witness: sk_A = 1, pk_A = G, b = 0, B1 = B2 = G,
        // PC_b = rho_b*H. Constrains nothing about the vote.
        Buffer skA(kEcScalarLength);  // 0x00..01
        skA.data()[kEcScalarLength - 1] = 1;
        secp256k1_pubkey pkA;
        BEAST_EXPECT(secp256k1_ec_pubkey_create(ctx, &pkA, skA.data()) == 1);
        secp256k1_pubkey const B1 = pkA;
        secp256k1_pubkey const B2 = pkA;
        auto const rhoB = ballot::randomScalar();
        auto const pcbBuf = ballot::pedersen(0, rhoB);
        secp256k1_pubkey pcb;
        BEAST_EXPECT(secp256k1_ec_pubkey_parse(ctx, &pcb, pcbBuf.data(), pcbBuf.size()) == 1);

        uint256 const ctxHash = sha512Half(std::uint16_t(0x5A5A), m);

        std::array<unsigned char, SECP256K1_COMPACT_STANDARD_PROOF_SIZE> proof{};
        BEAST_EXPECT(
            secp256k1_compact_standard_prove(
                ctx,
                proof.data(),
                m,
                /*balance=*/0,
                r.data(),
                skA.data(),
                rhoB.data(),
                /*n=*/1,
                &ct->c1,
                &ct->c2,
                &tallyPub,
                &pcm,
                &pkA,
                &pcb,
                &B1,
                &B2,
                ctxHash.data()) == 1);

        BEAST_EXPECT(
            secp256k1_compact_standard_verify(
                ctx,
                proof.data(),
                1,
                &ct->c1,
                &ct->c2,
                &tallyPub,
                &pcm,
                &pkA,
                &pcb,
                &B1,
                &B2,
                ctxHash.data()) == 1);

        // Tamper: PC_m commits to a different value than the ciphertext.
        auto const pcmBadBuf = ballot::pedersen(m + 1, r);
        secp256k1_pubkey pcmBad;
        BEAST_EXPECT(
            secp256k1_ec_pubkey_parse(ctx, &pcmBad, pcmBadBuf.data(), pcmBadBuf.size()) == 1);
        BEAST_EXPECT(
            secp256k1_compact_standard_verify(
                ctx,
                proof.data(),
                1,
                &ct->c1,
                &ct->c2,
                &tallyPub,
                &pcmBad,
                &pkA,
                &pcb,
                &B1,
                &B2,
                ctxHash.data()) == 0);
    }

    void
    testCredentialHappyPath()
    {
        testcase("Credential mode (1p1v) happy path");
        Env env{*this};
        Account const issuer{"issuer"};
        Account const member{"member"};
        env.fund(XRP(10000), issuer, member);
        env.close();

        // Permissioned domain owned by issuer, accepting a MEMBER credential.
        std::string const credType = "MEMBER";
        pdomain::Credentials const accepted{{.issuer = issuer, .credType = credType}};
        env(pdomain::setTx(issuer, accepted));
        auto const domainID = pdomain::getNewDomain(env.meta());
        env.close();

        // Issue and accept the credential for the member.
        env(credentials::create(member, issuer, credType));
        env.close();
        env(credentials::accept(member, issuer, credType));
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const nowSecs = env.now().time_since_epoch().count();
        auto const createSeq = env.seq(issuer);
        auto const ballotID = keylet::ballot(issuer.id(), createSeq).key;

        json::Value jv;
        jv[jss::TransactionType] = "BallotCreate";
        jv[jss::Account] = issuer.human();
        jv[sfDomainID.jsonName] = to_string(domainID);
        jv[sfDigest.jsonName] = std::string(64, 'A');
        jv[sfOptionCount.jsonName] = 2;
        jv[sfTallyPublicKey.jsonName] = strHex(tally.pub);
        jv[sfOpenTime.jsonName] = 0;
        jv[sfCloseTime.jsonName] = nowSecs + 1000;
        jv[sfZKProof.jsonName] = strHex(ballot::schnorrProof(tally, ballotID));
        jv[jss::Fee] = "1000";  // covers the confidential fee multiplier
        env(jv);
        env.close();

        // The member holds an accepted credential satisfying the domain, so
        // weight is 1. The linkage proof makes the encrypted cast verifiable
        // under the third-party tally key (no token, no lock).
        auto const castSeq = env.seq(member);
        auto const cast = makeBallotCast(
            tally,
            2,
            /*choice=*/0,
            /*weight=*/1,
            getBallotCastContextHash(member.id(), ballotID, castSeq));
        env(castVote(member, ballotID, cast));
        env.close();

        {
            auto const sle = env.le(keylet::ballot(ballotID));
            BEAST_EXPECT(sle && (*sle)[sfVoteCount] == 1);
        }

        // Close the window and finalize with per-option decryption proofs.
        env.close(2000s);
        auto const sle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(sle);
        auto const& tallyArr = sle->getFieldArray(sfEncryptedTally);
        std::vector<std::uint64_t> results;
        std::vector<Buffer> proofBufs;
        auto const finSeq = env.seq(issuer);
        auto const finCtx = getBallotFinalizeContextHash(issuer.id(), ballotID, finSeq);
        for (auto const& entry : tallyArr)
        {
            Slice const ct = entry[sfEncryptedVote];
            auto const amt = ballot::decrypt(ct, tally.priv, 100);
            BEAST_EXPECT(amt.has_value());
            results.push_back(*amt);
            proofBufs.push_back(ballot::decryptionProof(tally, *amt, ct, finCtx));
        }
        BEAST_EXPECT(results.size() == 2 && results[0] == 1 && results[1] == 0);

        Buffer proof(proofBufs.size() * kEcClawbackProofLength);
        for (std::size_t i = 0; i < proofBufs.size(); ++i)
            std::memcpy(
                proof.data() + (i * kEcClawbackProofLength),
                proofBufs[i].data(),
                kEcClawbackProofLength);

        env(finalize(issuer, ballotID, results, proof));
        env.close();

        auto const finSle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(finSle && finSle->isFlag(lsfBallotFinalized));
        auto const& res = finSle->getFieldArray(sfResults);
        BEAST_EXPECT(res.size() == 2 && res[0][sfBallotWeight] == 1 && res[1][sfBallotWeight] == 0);
    }

public:
    void
    testOptionCountAndGrace()
    {
        testcase("Option count and delete grace");
        using namespace jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const nowSecs = env.now().time_since_epoch().count();
        auto const seq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), seq).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, nowSecs - 10, nowSecs + 1000, ballotID));
        env.close();

        // A vote vector that does not match the ballot's OptionCount is a state
        // dependent failure, so it claims a fee rather than returning tem.
        {
            auto const castSeq = env.seq(bob);
            auto const cast = makeBallotCast(
                tally, 4, 0, 100, getBallotCastContextHash(bob.id(), ballotID, castSeq));
            env(castVote(bob, ballotID, cast), Ter(tecBALLOT_BAD_OPTIONS));
            env.close();
        }

        // The creator cannot delete an unfinalized ballot the moment voting
        // ends; the result must stay claimable for the grace period.
        env.close(std::chrono::seconds(1200));
        env(deleteBallot(alice, ballotID), Ter(tecTOO_SOON));
        env.close();

        // Past the grace period it may be deleted.
        env.close(std::chrono::seconds(90000));
        env(deleteBallot(alice, ballotID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ballot(ballotID)));
    }

    void
    run() override
    {
        testAmendmentDisabled();
        testTokenHappyPath();
        testCastFailures();
        testFinalizeFailures();
        testLinkageRoundTrip();
        testCredentialHappyPath();
        testOptionCountAndGrace();
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialVoting, app, xrpl);

}  // namespace test
}  // namespace xrpl
