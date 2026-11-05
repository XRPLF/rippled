#include <test/jtx/Account.h>
#include <test/jtx/Ballot.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <utility/mpt_utility.h>

#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;
using namespace std::chrono_literals;

class ConfidentialVoting_test : public beast::unit_test::Suite
{
    // Build a BallotFinalize transaction as raw JSON.
    static json::Value
    finalize(
        Account const& authority,
        UInt256 const& ballotID,
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
        return jv;
    }

    static json::Value
    deleteBallot(Account const& account, UInt256 const& ballotID)
    {
        json::Value jv;
        jv[jss::TransactionType] = "BallotDelete";
        jv[jss::Account] = account.human();
        jv[sfBallotID.jsonName] = to_string(ballotID);
        return jv;
    }

    // A permissioned domain owned by `owner` accepting a MEMBER credential,
    // issued by `owner` and accepted by `member`.
    static UInt256
    setupDomain(Env& env, Account const& owner, Account const& member)
    {
        std::string const credType = "MEMBER";
        pdomain::Credentials const accepted{{.issuer = owner, .credType = credType}};
        env(pdomain::setTx(owner, accepted));
        auto const domainID = pdomain::getNewDomain(env.meta());
        env.close();
        env(credentials::create(member, owner, credType));
        env.close();
        env(credentials::accept(member, owner, credType));
        env.close();
        return domainID;
    }

    // A BallotCastVote that also carries the cast's auditor vector, when it has
    // one, and the voter-recovery vector under `voterKey`.
    static json::Value
    castVoteMirrors(
        Account const& voter,
        UInt256 const& ballotID,
        BallotCast const& cast,
        std::optional<ballot::Keypair> const& voterKey)
    {
        auto jv = castVote(voter, ballotID, cast);
        if (!cast.auditorCiphertexts.empty())
        {
            jv[sfAuditorEncryptedVotes.jsonName] =
                ballotOptionArray(cast.auditorCiphertexts, nullptr);
        }
        if (voterKey)
        {
            jv[sfVoterPublicKey.jsonName] = strHex(voterKey->pub);
            jv[sfVoterEncryptedVotes.jsonName] = ballotOptionArray(cast.voterCiphertexts, nullptr);
        }
        return jv;
    }

    // The ciphertext encrypting the negated value: both components negated.
    static Buffer
    negateCiphertext(Buffer const& ct)
    {
        Buffer out(ct.data(), ct.size());
        out.data()[0] ^= 0x01;
        out.data()[kEcCiphertextComponentLength] ^= 0x01;
        return out;
    }

    // Per-option decryption proofs of `results` against the ballot's current
    // tally, for a BallotFinalize sent next by `authority`.
    static Buffer
    finalizeProof(
        Env& env,
        Account const& authority,
        UInt256 const& ballotID,
        ballot::Keypair const& tally,
        std::vector<std::uint64_t> const& results)
    {
        auto const sle = env.le(keylet::ballot(ballotID));
        if (!sle)
            return Buffer{};
        auto const& tallyArr = sle->getFieldArray(sfEncryptedTally);
        auto const finCtx =
            getBallotFinalizeContextHash(authority.id(), ballotID, env.seq(authority));
        Buffer proof(results.size() * kEcClawbackProofLength);
        for (std::size_t i = 0; i < results.size() && i < tallyArr.size(); ++i)
        {
            auto const p =
                ballot::decryptionProof(tally, results[i], tallyArr[i][sfEncryptedVote], finCtx);
            std::memcpy(
                proof.data() + (i * kEcClawbackProofLength), p.data(), kEcClawbackProofLength);
        }
        return proof;
    }

    // A cast by `voter` on `ballotID` for the voter's next sequence.
    static BallotCast
    castFor(
        Env& env,
        Account const& voter,
        UInt256 const& ballotID,
        ballot::Keypair const& tally,
        std::uint64_t weight,
        std::optional<ballot::Keypair> const& auditor = std::nullopt,
        std::optional<ballot::Keypair> const& voterKey = std::nullopt)
    {
        return makeBallotCast(
            tally,
            2,
            0,
            weight,
            getBallotCastContextHash(voter.id(), ballotID, env.seq(voter)),
            auditor,
            voterKey);
    }

    static std::uint32_t
    nowSeconds(Env& env)
    {
        return env.now().time_since_epoch().count();
    }

    void
    testAmendmentDisabled()
    {
        testcase("Amendment disabled");
        Env env{*this, testableAmendments() - featureConfidentialVoting};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        Account const bob{"bob"};
        env.fund(XRP(10000), bob);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 10);

        // Without the amendment a holder's balance has no vote lock.
        mpt.pay(bob, alice, 10);

        auto const tally = ballot::generateKeypair();
        auto const seq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), seq).key;
        auto const jv =
            createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, 0xFFFFFFFFu, ballotID);
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
        if (!sle)
            return;
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
            if (!amt)
                return;
            results.push_back(*amt);
            proofBufs.push_back(ballot::decryptionProof(tally, *amt, ct, finCtx));
        }
        BEAST_EXPECT(results.size() == 2 && results[0] == 100 && results[1] == 0);

        Buffer proof(proofBufs.size() * kEcClawbackProofLength);
        for (std::size_t i = 0; i < proofBufs.size(); ++i)
        {
            std::memcpy(
                proof.data() + (i * kEcClawbackProofLength),
                proofBufs[i].data(),
                kEcClawbackProofLength);
        }

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
        auto const* const ctx = mpt_secp256k1_context();

        auto const tally = ballot::generateKeypair();
        std::uint64_t const m = 42;
        auto const r = ballot::randomScalar();

        // ct = Enc(m; r) under the tally key; split into C1, C2.
        auto const ctBuf = ballot::encrypt(m, tally.pub, r);
        auto const ct = makeEcPair(ctBuf);
        BEAST_EXPECT(ct.has_value());
        if (!ct)
            return;

        secp256k1_pubkey tallyPub;
        BEAST_EXPECT(
            secp256k1_ec_pubkey_parse(ctx, &tallyPub, tally.pub.data(), tally.pub.size()) == 1);

        // PC_m = m*G + r*H (blinded by the ElGamal randomness r).
        auto const pcmBuf = ballot::pedersen(m, r);
        secp256k1_pubkey pcm;
        BEAST_EXPECT(secp256k1_ec_pubkey_parse(ctx, &pcm, pcmBuf.data(), pcmBuf.size()) == 1);

        // Vacuous balance witness: sk_A = 1, pk_A = G, b = 0, B1 = B2 = G,
        // PC_b = rho_b*H. Constrains nothing about the vote.
        std::array<std::uint8_t, kEcScalarLength> skA{};  // 0x00..01
        skA.back() = 1;
        secp256k1_pubkey pkA;
        BEAST_EXPECT(secp256k1_ec_pubkey_create(ctx, &pkA, skA.data()) == 1);
        secp256k1_pubkey const b1 = pkA;
        secp256k1_pubkey const b2 = pkA;
        auto const rhoB = ballot::randomScalar();
        auto const pcbBuf = ballot::pedersen(0, rhoB);
        secp256k1_pubkey pcb;
        BEAST_EXPECT(secp256k1_ec_pubkey_parse(ctx, &pcb, pcbBuf.data(), pcbBuf.size()) == 1);

        UInt256 const ctxHash = sha512Half(std::uint16_t(0x5A5A), m);

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
                &b1,
                &b2,
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
                &b1,
                &b2,
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
                &b1,
                &b2,
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

        auto const domainID = setupDomain(env, issuer, member);

        auto const tally = ballot::generateKeypair();
        auto const nowSecs = env.now().time_since_epoch().count();
        auto const createSeq = env.seq(issuer);
        auto const ballotID = keylet::ballot(issuer.id(), createSeq).key;

        env(createDomainBallot(issuer, domainID, tally, 2, 0, nowSecs + 1000, ballotID));
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
        if (!sle)
            return;
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
            if (!amt)
                return;
            results.push_back(*amt);
            proofBufs.push_back(ballot::decryptionProof(tally, *amt, ct, finCtx));
        }
        BEAST_EXPECT(results.size() == 2 && results[0] == 1 && results[1] == 0);

        Buffer proof(proofBufs.size() * kEcClawbackProofLength);
        for (std::size_t i = 0; i < proofBufs.size(); ++i)
        {
            std::memcpy(
                proof.data() + (i * kEcClawbackProofLength),
                proofBufs[i].data(),
                kEcClawbackProofLength);
        }

        env(finalize(issuer, ballotID, results, proof));
        env.close();

        auto const finSle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(finSle && finSle->isFlag(lsfBallotFinalized));
        auto const& res = finSle->getFieldArray(sfResults);
        BEAST_EXPECT(res.size() == 2 && res[0][sfBallotWeight] == 1 && res[1][sfBallotWeight] == 0);
    }

    void
    testCreatePreflight()
    {
        testcase("Create preflight");
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        MPTTester mpt(env, alice, {.fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});

        auto const tally = ballot::generateKeypair();
        auto const now = nowSeconds(env);
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        auto const good = [&] {
            return createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 1000, ballotID);
        };
        std::string const badPoint = "04" + std::string(64, '0');

        // Exactly one eligibility source.
        {
            auto jv = good();
            jv[sfDomainID.jsonName] = to_string(UInt256{1u});
            env(jv, Ter(temMALFORMED));
        }
        env(createBallot(alice, tally, 2, 0, now + 1000, ballotID), Ter(temMALFORMED));
        env(createDomainBallot(alice, UInt256{}, tally, 2, 0, now + 1000, ballotID),
            Ter(temMALFORMED));

        // Between 2 and 8 options.
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 1, 0, now + 1000, ballotID),
            Ter(temINVALID_COUNT));
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 9, 0, now + 1000, ballotID),
            Ter(temINVALID_COUNT));

        // A non-empty voting window.
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, now + 1000, now + 1000, ballotID),
            Ter(temMALFORMED));

        // Keys must be compressed points; the Schnorr proof has a fixed size.
        {
            auto jv = good();
            jv[sfTallyPublicKey.jsonName] = badPoint;
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfZKProof.jsonName] = "00";
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfAuditorEncryptionKey.jsonName] = badPoint;
            env(jv, Ter(temMALFORMED));
        }

        // A URI is 1 to 256 bytes.
        {
            auto jv = good();
            jv[sfURI.jsonName] = "";
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfURI.jsonName] = std::string(2 * 257, 'A');
            env(jv, Ter(temMALFORMED));
        }

        // Only tfVoterRecoverable is a valid flag.
        env(good(), Txflags(0x00010000), Ter(temINVALID_FLAG));

        // Credential mode needs PermissionedDomains.
        {
            Env env2{*this, testableAmendments() - featurePermissionedDomains};
            env2.fund(XRP(10000), alice);
            env2.close();
            env2(
                createDomainBallot(
                    alice,
                    UInt256{1u},
                    tally,
                    2,
                    0,
                    now + 1000,
                    keylet::ballot(alice.id(), env2.seq(alice)).key),
                Ter(temDISABLED));
        }

        // Credential mode needs Credentials.
        {
            Env env2{*this, testableAmendments() - featureCredentials};
            env2.fund(XRP(10000), alice);
            env2.close();
            env2(
                createDomainBallot(
                    alice,
                    UInt256{1u},
                    tally,
                    2,
                    0,
                    now + 1000,
                    keylet::ballot(alice.id(), env2.seq(alice)).key),
                Ter(temDISABLED));
        }

        // A URI, an auditor key and tfVoterRecoverable are stored on the ballot.
        auto const auditor = ballot::generateKeypair();
        auto jv = good();
        jv[sfURI.jsonName] = strHex(std::string("https://example.com/ballot"));
        jv[sfAuditorEncryptionKey.jsonName] = strHex(auditor.pub);
        env(jv, Txflags(tfVoterRecoverable));
        env.close();
        auto const sle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(sle);
        if (!sle)
            return;
        BEAST_EXPECT(sle->isFieldPresent(sfURI));
        BEAST_EXPECT((*sle)[sfAuditorEncryptionKey] == auditor.pub);
        BEAST_EXPECT(sle->isFlag(lsfVoterRecoverable));
        BEAST_EXPECT(sle->getFieldArray(sfEncryptedTally).size() == 2);
    }

    void
    testCreatePreclaim()
    {
        testcase("Create preclaim");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        MPTTester mpt(env, alice, {.fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});

        auto const tally = ballot::generateKeypair();
        auto const now = nowSeconds(env);
        auto const nextBallot = [&](Account const& owner) {
            return keylet::ballot(owner.id(), env.seq(owner)).key;
        };

        // The Schnorr proof is bound to the ballot's own index.
        {
            auto const otherID = keylet::ballot(alice.id(), env.seq(alice) + 1).key;
            env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 1000, otherID),
                Ter(tecBAD_PROOF));
        }

        // The issuance must exist and belong to the creator.
        env(createTokenBallot(
                alice, makeMptID(999, alice.id()), tally, 2, 0, now + 1000, nextBallot(alice)),
            Ter(tecOBJECT_NOT_FOUND));
        env(createTokenBallot(bob, mpt.issuanceID(), tally, 2, 0, now + 1000, nextBallot(bob)),
            Ter(tecNO_PERMISSION));

        // One open ballot per issuance; a closed one no longer blocks.
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 100, nextBallot(alice)));
        env.close();
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 5000, nextBallot(alice)),
            Ter(tecBALLOT_EXISTS));
        env.close(200s);
        auto const second = nextBallot(alice);
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 5000, second));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::mptokenIssuance(mpt.issuanceID())) &&
            (*env.le(keylet::mptokenIssuance(mpt.issuanceID())))[sfBallotID] == second);
        env(createTokenBallot(alice, mpt.issuanceID(), tally, 2, 0, now + 5000, nextBallot(alice)),
            Ter(tecBALLOT_EXISTS));

        // The domain must exist and belong to the creator.
        auto const domainID = setupDomain(env, alice, bob);
        env(createDomainBallot(alice, UInt256{5u}, tally, 2, 0, now + 1000, nextBallot(alice)),
            Ter(tecOBJECT_NOT_FOUND));
        env(createDomainBallot(bob, domainID, tally, 2, 0, now + 1000, nextBallot(bob)),
            Ter(tecNO_PERMISSION));
    }

    void
    testCreateReserve()
    {
        testcase("Create reserve");
        Env env{*this};
        Account const alice{"alice"};
        auto const reserve = env.current()->fees().accountReserve(1, 1);
        env.fund(drops(reserve + XRP(1).value().xrp()), alice);
        env.close();

        MPTTester mpt(env, alice, {.fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});

        auto const tally = ballot::generateKeypair();
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        env(createTokenBallot(
                alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID),
            Ter(tecINSUFFICIENT_RESERVE));
        BEAST_EXPECT(!env.le(keylet::ballot(ballotID)));
    }

    void
    testCastPreflight()
    {
        testcase("Cast preflight");
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const tally = ballot::generateKeypair();
        UInt256 const ballotID{7u};
        auto const cast = makeBallotCast(tally, 2, 0, 1, UInt256{});
        auto const good = [&] { return castVote(alice, ballotID, cast); };
        std::string const badPoint = "04" + std::string(64, '0');
        auto const option = [](json::Value& jv, json::StaticString const& field) -> json::Value& {
            return jv[field][0u][sfBallotOption.jsonName];
        };

        // One to eight options.
        {
            auto jv = good();
            jv[sfEncryptedVotes.jsonName] = json::Value(json::ValueType::Array);
            env(jv, Ter(temMALFORMED));
        }
        {
            std::vector<Buffer> const cts(9, cast.ciphertexts[0]);
            std::vector<Buffer> const cms(9, cast.commitments[0]);
            std::vector<Buffer> const prs(9, cast.linkageProofs[0]);
            auto jv = good();
            jv[sfEncryptedVotes.jsonName] = ballotOptionArray(cts, &cms, &prs);
            env(jv, Ter(temMALFORMED));
        }

        // Each primary entry: a 66-byte ciphertext, a commitment and a linkage proof.
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName)[sfEncryptedVote.jsonName] =
                strHex(cast.ciphertexts[0]).substr(2);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName)[sfEncryptedVote.jsonName] = badPoint + badPoint;
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName).removeMember(sfAmountCommitment.jsonName);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName)[sfAmountCommitment.jsonName] = badPoint;
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName).removeMember(sfZKProof.jsonName);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            option(jv, sfEncryptedVotes.jsonName)[sfZKProof.jsonName] = "00";
            env(jv, Ter(temBAD_CIPHERTEXT));
        }

        // Auditor vector: same length, ciphertexts only.
        {
            auto jv = good();
            jv[sfAuditorEncryptedVotes.jsonName] =
                ballotOptionArray({cast.ciphertexts[0]}, nullptr);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }
        {
            auto jv = good();
            jv[sfAuditorEncryptedVotes.jsonName] =
                ballotOptionArray(cast.ciphertexts, &cast.commitments);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }

        // Voter recovery: key and vector together, a valid key, the same length.
        {
            auto jv = good();
            jv[sfVoterPublicKey.jsonName] = strHex(tally.pub);
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfVoterEncryptedVotes.jsonName] = ballotOptionArray(cast.ciphertexts, nullptr);
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfVoterPublicKey.jsonName] = badPoint;
            jv[sfVoterEncryptedVotes.jsonName] = ballotOptionArray(cast.ciphertexts, nullptr);
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = good();
            jv[sfVoterPublicKey.jsonName] = strHex(tally.pub);
            jv[sfVoterEncryptedVotes.jsonName] = ballotOptionArray({cast.ciphertexts[0]}, nullptr);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }

        {
            auto jv = good();
            jv[sfVoterPublicKey.jsonName] = strHex(tally.pub);
            jv[sfVoterEncryptedVotes.jsonName] =
                ballotOptionArray(cast.ciphertexts, &cast.commitments);
            env(jv, Ter(temBAD_CIPHERTEXT));
        }

        // A range proof is required.
        {
            auto jv = good();
            jv[sfZKProof.jsonName] = "";
            env(jv, Ter(temMALFORMED));
        }

        // CredentialIDs, when present, are checked like any credential list.
        {
            auto jv = good();
            jv[sfCredentialIDs.jsonName] = json::Value(json::ValueType::Array);
            env(jv, Ter(temMALFORMED));
        }
        {
            Env env2{*this, testableAmendments() - featureCredentials};
            env2.fund(XRP(10000), alice);
            env2.close();
            auto jv = castVote(alice, ballotID, cast);
            jv[sfCredentialIDs.jsonName] = json::Value(json::ValueType::Array);
            jv[sfCredentialIDs.jsonName].append(to_string(UInt256{3u}));
            env2(jv, Ter(temDISABLED));
        }
    }

    void
    testCastPreclaim()
    {
        testcase("Cast preclaim");
        Env env{*this};
        Account const owner{"owner"};
        Account const member{"member"};
        Account const outsider{"outsider"};
        env.fund(XRP(10000), owner, member, outsider);
        env.close();

        auto const domainID = setupDomain(env, owner, member);
        auto const tally = ballot::generateKeypair();
        auto const auditor = ballot::generateKeypair();
        auto const voterKey = ballot::generateKeypair();
        auto const now = nowSeconds(env);

        auto const newBallot = [&](std::optional<ballot::Keypair> const& aud, bool recoverable) {
            auto const id = keylet::ballot(owner.id(), env.seq(owner)).key;
            auto jv = createDomainBallot(owner, domainID, tally, 2, 0, now + 10000, id);
            if (aud)
                jv[sfAuditorEncryptionKey.jsonName] = strHex(aud->pub);
            if (recoverable)
                jv[jss::Flags] = tfVoterRecoverable;
            env(jv);
            env.close();
            return id;
        };

        auto const plain = newBallot(std::nullopt, false);

        // No such ballot.
        {
            UInt256 const missing{9u};
            env(castVote(member, missing, castFor(env, member, missing, tally, 1)),
                Ter(tecNO_ENTRY));
        }

        // Credential mode: only domain members vote.
        env(castVote(outsider, plain, castFor(env, outsider, plain, tally, 1)),
            Ter(tecNO_PERMISSION));

        // Mirror vectors must match the ballot's configuration.
        env(castVoteMirrors(
                member, plain, castFor(env, member, plain, tally, 1, auditor), std::nullopt),
            Ter(tecNO_PERMISSION));
        env(castVoteMirrors(
                member,
                plain,
                castFor(env, member, plain, tally, 1, std::nullopt, voterKey),
                voterKey),
            Ter(tecNO_PERMISSION));

        // The vote vector must sum to the eligible weight under the cast's randomness.
        env(castVote(member, plain, castFor(env, member, plain, tally, 2)), Ter(tecBAD_PROOF));
        {
            auto cast = castFor(env, member, plain, tally, 1);
            cast.ciphertexts[1] = negateCiphertext(cast.ciphertexts[0]);
            env(castVote(member, plain, cast), Ter(tecBAD_PROOF));
        }
        {
            auto cast = castFor(env, member, plain, tally, 1);
            std::array<std::uint8_t, kEcScalarLength> const zero{};
            cast.blinding = Buffer(zero.data(), zero.size());
            env(castVote(member, plain, cast), Ter(tecBAD_PROOF));
        }

        // Each option's ciphertext must be linked to its commitment.
        {
            auto cast = castFor(env, member, plain, tally, 1);
            cast.linkageProofs[0].data()[40] ^= 0x01;
            env(castVote(member, plain, cast), Ter(tecBAD_PROOF));
        }

        // A ballot with an auditor key.
        auto const audited = newBallot(auditor, false);
        env(castVote(member, audited, castFor(env, member, audited, tally, 1)),
            Ter(tecNO_PERMISSION));
        {
            auto cast = castFor(env, member, audited, tally, 1, auditor);
            auto const other = castFor(env, member, audited, tally, 1, auditor);
            cast.auditorCiphertexts = other.auditorCiphertexts;
            env(castVoteMirrors(member, audited, cast, std::nullopt), Ter(tecBAD_PROOF));
        }
        {
            auto cast = castFor(env, member, audited, tally, 1, auditor);
            std::swap(cast.auditorCiphertexts[0], cast.auditorCiphertexts[1]);
            env(castVoteMirrors(member, audited, cast, std::nullopt), Ter(tecBAD_PROOF));
        }
        env(castVoteMirrors(
            member, audited, castFor(env, member, audited, tally, 1, auditor), std::nullopt));
        env.close();
        {
            auto const vote = env.le(keylet::ballotVote(audited, member.id()));
            BEAST_EXPECT(vote && vote->isFieldPresent(sfAuditorEncryptedVotes));
            BEAST_EXPECT(vote && !vote->isFieldPresent(sfVoterPublicKey));
        }

        // A voter-recoverable ballot.
        auto const recoverable = newBallot(std::nullopt, true);
        env(castVote(member, recoverable, castFor(env, member, recoverable, tally, 1)),
            Ter(tecNO_PERMISSION));
        {
            auto cast = castFor(env, member, recoverable, tally, 1, std::nullopt, voterKey);
            auto const other = castFor(env, member, recoverable, tally, 1, std::nullopt, voterKey);
            cast.voterCiphertexts = other.voterCiphertexts;
            env(castVoteMirrors(member, recoverable, cast, voterKey), Ter(tecBAD_PROOF));
        }
        {
            auto cast = castFor(env, member, recoverable, tally, 1, std::nullopt, voterKey);
            std::swap(cast.voterCiphertexts[0], cast.voterCiphertexts[1]);
            env(castVoteMirrors(member, recoverable, cast, voterKey), Ter(tecBAD_PROOF));
        }
        env(castVoteMirrors(
            member,
            recoverable,
            castFor(env, member, recoverable, tally, 1, std::nullopt, voterKey),
            voterKey));
        env.close();
        {
            auto const vote = env.le(keylet::ballotVote(recoverable, member.id()));
            BEAST_EXPECT(vote && (*vote)[sfVoterPublicKey] == voterKey.pub);
            BEAST_EXPECT(vote && vote->isFieldPresent(sfVoterEncryptedVotes));
            BEAST_EXPECT(vote && (*vote)[sfBallotWeight] == 1);
        }
    }

    void
    testVoteLock()
    {
        testcase("Vote lock");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const erin{"erin"};
        env.fund(XRP(10000), alice, bob, carol, erin);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob, carol, erin}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.authorize({.account = erin});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID));
        env.close();

        // An empty MPToken carries no weight.
        env(castVote(erin, ballotID, castFor(env, erin, ballotID, tally, 1)), Ter(tecNO_ENTRY));

        env(castVote(bob, ballotID, castFor(env, bob, ballotID, tally, 100)));
        env.close();

        // The voted balance cannot move while the ballot is open.
        mpt.pay(bob, carol, 1, tecPATH_DRY);

        // Tokens received after the cast are not locked.
        mpt.pay(alice, bob, 10);
        mpt.pay(bob, carol, 10);
        mpt.pay(bob, carol, 1, tecPATH_DRY);

        // Closing the window releases the lock.
        env.close(2000s);
        mpt.pay(bob, carol, 100);
        BEAST_EXPECT(mpt.getBalance(carol) == 110);
    }

    void
    testCastReserve()
    {
        testcase("Cast reserve");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const reserve = env.current()->fees().accountReserve(1, 1);
        env.fund(XRP(10000), alice);
        env.fund(drops(reserve + XRP(1).value().xrp()), bob);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID));
        env.close();

        env(castVote(bob, ballotID, castFor(env, bob, ballotID, tally, 100)),
            Ter(tecINSUFFICIENT_RESERVE));
        BEAST_EXPECT(!env.le(keylet::ballotVote(ballotID, bob.id())));
        auto const sle = env.le(keylet::ballot(ballotID));
        BEAST_EXPECT(sle && (*sle)[sfVoteCount] == 0);
    }

    void
    testFinalizeChecks()
    {
        testcase("Finalize checks");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const dave{"dave"};
        env.fund(XRP(10000), alice, bob, dave);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob, dave}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = dave});
        mpt.pay(alice, bob, 100);
        mpt.pay(alice, dave, 5);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID));
        env.close();
        env(castVote(bob, ballotID, castFor(env, bob, ballotID, tally, 100)));
        env.close();

        auto const proofOf = [](std::size_t count) {
            std::vector<std::uint8_t> const bytes(count, 0);
            return Buffer(bytes.data(), bytes.size());
        };

        // One to eight results, one proof per result.
        env(finalize(alice, ballotID, {}, proofOf(kEcClawbackProofLength)), Ter(temMALFORMED));
        env(finalize(
                alice,
                ballotID,
                std::vector<std::uint64_t>(9, 0),
                proofOf(9 * kEcClawbackProofLength)),
            Ter(temMALFORMED));
        env(finalize(alice, ballotID, {100, 0}, Buffer{}), Ter(temMALFORMED));
        env(finalize(alice, ballotID, {100, 0}, proofOf(kEcClawbackProofLength + 1)),
            Ter(temMALFORMED));
        env(finalize(alice, ballotID, {100, 0}, proofOf(kEcClawbackProofLength)),
            Ter(temMALFORMED));

        // No such ballot.
        env(finalize(alice, UInt256{9u}, {100, 0}, proofOf(2 * kEcClawbackProofLength)),
            Ter(tecNO_ENTRY));

        env.close(2000s);

        // One result per option.
        env(finalize(alice, ballotID, {100, 0, 0}, proofOf(3 * kEcClawbackProofLength)),
            Ter(tecBALLOT_BAD_OPTIONS));

        env(finalize(
            alice, ballotID, {100, 0}, finalizeProof(env, alice, ballotID, tally, {100, 0})));
        env.close();

        // A finalized ballot takes no second result and no further cast.
        env(finalize(
                alice, ballotID, {100, 0}, finalizeProof(env, alice, ballotID, tally, {100, 0})),
            Ter(tecDUPLICATE));
        env(castVote(dave, ballotID, castFor(env, dave, ballotID, tally, 5)),
            Ter(tecBALLOT_CLOSED));
    }

    void
    testDeleteChecks()
    {
        testcase("Delete checks");
        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const dave{"dave"};
        env.fund(XRP(10000), alice, bob, dave);
        env.close();

        MPTTester mpt(env, alice, {.holders = {bob, dave}, .fund = false});
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = dave});
        mpt.pay(alice, bob, 100);
        mpt.pay(alice, dave, 5);
        env.close();

        auto const tally = ballot::generateKeypair();
        auto const ballotID = keylet::ballot(alice.id(), env.seq(alice)).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID));
        env.close();
        env(castVote(bob, ballotID, castFor(env, bob, ballotID, tally, 100)));
        env(castVote(dave, ballotID, castFor(env, dave, ballotID, tally, 5)));
        env.close();

        env(deleteBallot(alice, UInt256{}), Ter(temMALFORMED));
        env(deleteBallot(alice, UInt256{9u}), Ter(tecNO_ENTRY));
        env(deleteBallot(bob, UInt256{9u}), Ter(tecNO_ENTRY));
        Account const carol{"carol"};
        env.fund(XRP(10000), carol);
        env.close();
        env(deleteBallot(carol, ballotID), Ter(tecNO_PERMISSION));

        // A voter keeps the cast record while the ballot is open.
        env(deleteBallot(bob, ballotID), Ter(tecTOO_SOON));
        env.close();

        env.close(2000s);
        auto const bobOwners = env.le(keylet::account(bob.id()))->getFieldU32(sfOwnerCount);
        env(deleteBallot(bob, ballotID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ballotVote(ballotID, bob.id())));
        BEAST_EXPECT(env.le(keylet::account(bob.id()))->getFieldU32(sfOwnerCount) == bobOwners - 1);

        // A cast record outlives its deleted ballot and can still be removed.
        // Both casts went to option 0.
        env(finalize(
            alice, ballotID, {105, 0}, finalizeProof(env, alice, ballotID, tally, {105, 0})));
        env.close();
        env(deleteBallot(alice, ballotID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ballot(ballotID)));

        // A lock naming a deleted ballot locks nothing.
        mpt.pay(bob, alice, 100);

        env(deleteBallot(dave, ballotID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ballotVote(ballotID, dave.id())));

        // The issuance's marker to the deleted ballot does not block a new one.
        env(createTokenBallot(
            alice,
            mpt.issuanceID(),
            tally,
            2,
            0,
            nowSeconds(env) + 1000,
            keylet::ballot(alice.id(), env.seq(alice)).key));
        env.close();
    }

    void
    testLedgerEntry()
    {
        testcase("ledger_entry");
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
        auto const createSeq = env.seq(alice);
        auto const ballotID = keylet::ballot(alice.id(), createSeq).key;
        env(createTokenBallot(
            alice, mpt.issuanceID(), tally, 2, 0, nowSeconds(env) + 1000, ballotID));
        env.close();
        env(castVote(bob, ballotID, castFor(env, bob, ballotID, tally, 100)));
        env.close();
        auto const voteID = keylet::ballotVote(ballotID, bob.id()).key;

        auto const request = [&](json::Value const& params) {
            return env.rpc("json", "ledger_entry", json::to_string(params))[jss::result];
        };

        {
            json::Value params;
            params[jss::ballot][jss::owner] = alice.human();
            params[jss::ballot][jss::seq] = createSeq;
            BEAST_EXPECT(request(params)[jss::index] == to_string(ballotID));
        }
        {
            json::Value params;
            params[jss::ballot] = to_string(ballotID);
            BEAST_EXPECT(request(params)[jss::index] == to_string(ballotID));
        }
        {
            json::Value params;
            params[jss::ballot_vote][jss::ballot_id] = to_string(ballotID);
            params[jss::ballot_vote][jss::account] = bob.human();
            BEAST_EXPECT(request(params)[jss::index] == to_string(voteID));
        }
        {
            json::Value params;
            params[jss::ballot_vote] = to_string(voteID);
            BEAST_EXPECT(request(params)[jss::index] == to_string(voteID));
        }
        {
            json::Value params;
            params[jss::ballot][jss::owner] = "bad";
            params[jss::ballot][jss::seq] = createSeq;
            BEAST_EXPECT(request(params)[jss::error] == "malformedOwner");
        }
        {
            json::Value params;
            params[jss::ballot][jss::owner] = alice.human();
            params[jss::ballot][jss::seq] = "x";
            BEAST_EXPECT(request(params)[jss::error] == "malformedSeq");
        }
        {
            json::Value params;
            params[jss::ballot_vote][jss::ballot_id] = "zz";
            params[jss::ballot_vote][jss::account] = bob.human();
            BEAST_EXPECT(request(params)[jss::error] == "malformedBallotID");
        }
        {
            json::Value params;
            params[jss::ballot_vote][jss::ballot_id] = to_string(ballotID);
            params[jss::ballot_vote][jss::account] = "bad";
            BEAST_EXPECT(request(params)[jss::error] == "malformedAccount");
        }
    }

    void
    testProofHelpers()
    {
        testcase("Ballot proof helpers");
        auto const tally = ballot::generateKeypair();
        UInt256 const ctxHash{11u};
        auto const cast = makeBallotCast(tally, 2, 0, 1, ctxHash);

        Slice const ct = cast.ciphertexts[0];
        Slice const c1{ct.data(), kEcCiphertextComponentLength};
        Slice const c2{ct.data() + kEcCiphertextComponentLength, kEcCiphertextComponentLength};
        Slice const commitment = cast.commitments[0];
        Slice const proof = cast.linkageProofs[0];
        std::vector<Slice> const keys{tally.pub};
        std::vector<Slice> const c2s{c2};

        std::vector<std::uint8_t> badPoint(kEcPubKeyLength, 0);
        badPoint[0] = 0x04;
        Slice const bad{badPoint.data(), badPoint.size()};
        Slice const shortPoint{ct.data(), kEcCiphertextComponentLength - 1};

        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, c2s, commitment, proof, ctxHash) == tesSUCCESS);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, c2s, commitment, proof, UInt256{12u}) ==
            tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(
                keys, c1, c2s, commitment, Slice{proof.data(), proof.size() - 1}, ctxHash) ==
            tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, shortPoint, c2s, commitment, proof, ctxHash) ==
            tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, c2s, shortPoint, proof, ctxHash) == tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, bad, c2s, commitment, proof, ctxHash) == tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, {shortPoint}, commitment, proof, ctxHash) ==
            tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage({shortPoint}, c1, c2s, commitment, proof, ctxHash) ==
            tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, {bad}, commitment, proof, ctxHash) == tecBAD_PROOF);
        BEAST_EXPECT(
            verifyBallotVoteLinkage({bad}, c1, c2s, commitment, proof, ctxHash) == tecBAD_PROOF);
        BEAST_EXPECT(verifyBallotVoteLinkage(keys, c1, c2s, bad, proof, ctxHash) == tecBAD_PROOF);

        // One to three mirror keys, one C2 per key.
        BEAST_EXPECT(
            verifyBallotVoteLinkage({}, c1, {}, commitment, proof, ctxHash) == tecINTERNAL);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(
                {tally.pub, tally.pub, tally.pub, tally.pub},
                c1,
                {c2, c2, c2, c2},
                commitment,
                proof,
                ctxHash) == tecINTERNAL);
        BEAST_EXPECT(
            verifyBallotVoteLinkage(keys, c1, {c2, c2}, commitment, proof, ctxHash) == tecINTERNAL);

        // Range-proof commitments are 33 bytes each.
        BEAST_EXPECT(
            verifyBallotRangeProof(cast.proof, {cast.commitments[0], shortPoint}, ctxHash) ==
            tecINTERNAL);

        std::vector<Slice> const commitments{cast.commitments[0], cast.commitments[1]};
        BEAST_EXPECT(verifyBallotRangeProof(cast.proof, commitments, ctxHash) == tesSUCCESS);
        BEAST_EXPECT(verifyBallotRangeProof(cast.proof, commitments, UInt256{12u}) == tecBAD_PROOF);
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
        testCreatePreflight();
        testCreatePreclaim();
        testCreateReserve();
        testCastPreflight();
        testCastPreclaim();
        testVoteLock();
        testCastReserve();
        testFinalizeChecks();
        testDeleteChecks();
        testLedgerEntry();
        testProofHelpers();
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialVoting, app, xrpl);

}  // namespace xrpl::test
