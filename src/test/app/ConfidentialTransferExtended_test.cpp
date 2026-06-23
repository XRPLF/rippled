#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delegate.h>
#include <test/jtx/deposit.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/owners.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace xrpl {

class ConfidentialTransferExtended_test : public ConfidentialTransferTestBase
{
    void
    testSendDepositPreauth(FeatureBitset features)
    {
        testcase("Send deposit preauth");
        using namespace test::jtx;

        // When an account enables lsfDepositAuth (via asfDepositAuth flag),
        // it requires explicit authorization before accepting incoming payments.
        //
        // There are two authorization mechanisms:
        //
        // 1. DIRECT ACCOUNT AUTHORIZATION (deposit::auth)
        //    - Bob directly authorizes Carol: deposit::auth(bob, carol)
        //    - Simple 1-to-1 trust relationship
        //    - Carol can send to Bob without credentials
        //
        // 2. CREDENTIAL-BASED AUTHORIZATION (deposit::authCredentials)
        //    - A trusted third party (dpIssuer) issues credentials
        //    - Bob authorizes a credential TYPE from an issuer
        //    - Anyone holding that credential can send to Bob
        //    - Requires sender to include credential ID in transaction

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dpIssuer("dpIssuer");
        char const credType[] = "KYC_VERIFIED";

        // Create and accept credential for an account
        auto createCredential = [&](Env& env, Account const& subject) -> std::string {
            env(credentials::create(subject, dpIssuer, credType));
            env.close();
            env(credentials::accept(subject, dpIssuer, credType));
            env.close();
            auto const jv = credentials::ledgerEntry(env, subject, dpIssuer, credType);
            return jv[jss::result][jss::index].asString();
        };

        // TEST 1: Direct Account Authorization
        {
            Env env(*this, features);
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;
            env(fset(bob, asfDepositAuth));
            env.close();

            // Carol cannot send to Bob without authorization
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // Bob directly authorizes Carol
            env(deposit::auth(bob, carol));
            env.close();

            // Now Carol can send to Bob
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
            });
            mpt.mergeInbox({
                .account = bob,
            });

            // Bob revokes Carol's authorization
            env(deposit::unauth(bob, carol));
            env.close();

            // Carol can no longer send to Bob
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });
        }

        // TEST 2: Credential-Based Authorization
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;
            env(fset(bob, asfDepositAuth));
            env.close();

            auto const credIdx = createCredential(env, carol);

            // Carol cannot send yet - Bob hasn't authorized this credential type
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecNO_PERMISSION,
            });

            // Bob authorizes the credential type from dpIssuer
            env(deposit::authCredentials(bob, {{.issuer = dpIssuer, .credType = credType}}));
            env.close();

            // Carol still cannot send without including credential
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // Carol CAN send when including her credential
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
            mpt.mergeInbox({
                .account = bob,
            });
        }

        // TEST 3: Direct Auth Takes Precedence Over Credentials
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;
            env(fset(bob, asfDepositAuth));
            env.close();

            auto const credIdx = createCredential(env, carol);

            // Bob directly authorizes Carol (no credential needed)
            env(deposit::auth(bob, carol));
            env.close();

            // Carol can send without credentials (direct auth)
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
            });
            mpt.mergeInbox({
                .account = bob,
            });

            // Carol can also send WITH credentials (still works)
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
            mpt.mergeInbox({
                .account = bob,
            });

            // Bob revokes direct authorization
            env(deposit::unauth(bob, carol));
            env.close();

            // Carol cannot send without credentials anymore
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // But credential-based auth not set up, so this also fails
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecNO_PERMISSION,
            });

            // Bob authorizes the credential type
            env(deposit::authCredentials(bob, {{.issuer = dpIssuer, .credType = credType}}));
            env.close();

            // Now Carol can send with credentials
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
        }

        auto const expireTime = 30;

        // Lambda function that returns the credential index after creating a
        // credential that expires shortly after the current ledger time.
        auto createExpiringCredential = [&](Env& env, Account const& subject) -> std::string {
            auto jv = credentials::create(subject, dpIssuer, credType);
            auto const expiry =
                env.current()->header().parentCloseTime.time_since_epoch().count() + expireTime;
            jv[sfExpiration.jsonName] = expiry;
            env(jv);
            env.close();
            env(credentials::accept(subject, dpIssuer, credType));
            env.close();
            auto const credentials = credentials::ledgerEntry(env, subject, dpIssuer, credType);
            return credentials[jss::result][jss::index].asString();
        };

        auto credentialDeleted = [&](Env& env, Account const& subject) -> bool {
            auto const credentials = credentials::ledgerEntry(env, subject, dpIssuer, credType);
            return credentials[jss::result].isMember(jss::error) &&
                credentials[jss::result][jss::error] == "entryNotFound";
        };

        // TEST 4: Expired credential with matching depositPreauth entry.
        // checkDepositPreauth in preclaim returns tesSUCCESS (the expired
        // credential still exists and matches the depositPreauth key), so ZK
        // proofs run. cleanupExpiredCredentials in doApply then removes the
        // expired credential and returns tecEXPIRED.
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;
            env(fset(bob, asfDepositAuth));
            env.close();

            auto const credIdx = createExpiringCredential(env, carol);

            // Bob authorizes carol's credential type
            env(deposit::authCredentials(bob, {{.issuer = dpIssuer, .credType = credType}}));
            env.close();

            // Advance ledger past credential expiration
            env.close(std::chrono::seconds(expireTime));

            // Send fails with tecEXPIRED; the expired credential is cleaned up
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecEXPIRED,
            });
            env.close();

            BEAST_EXPECT(credentialDeleted(env, carol));
        }

        // TEST 5: Expired credential, destination has no depositAuth.
        // checkDepositPreauth in preclaim returns tesSUCCESS even with expired credentials,
        // because we want to keep the checkDepositPreauth part before the expensive proof
        // verification. cleanupExpiredCredentials in doApply removes the expired credential and
        // returns tecEXPIRED.
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            auto const credIdx = createExpiringCredential(env, carol);

            // Advance ledger past credential expiration
            env.close(std::chrono::seconds(expireTime));

            // Send fails with tecEXPIRED; the expired credential is cleaned up
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecEXPIRED,
            });
            env.close();

            BEAST_EXPECT(credentialDeleted(env, carol));
        }

        // TEST 6: Expired credential, depositAuth enabled but credential
        // not authorized by bob.
        // checkDepositPreauth in preclaim calls checkDepositPreauth which
        // finds no match and returns tecNO_PERMISSION. doApply never runs, so
        // the expired credential is not cleaned up by this transaction. This is
        // a deliberate tradeoff: allowing doApply to run solely for cleanup
        // would require bypassing the preclaim short-circuit, forcing every
        // validator to run the expensive ZK proof verification before
        // discovering the authorization failure. Expired credentials here will
        // be cleaned up opportunistically by a future transaction that
        // references them.
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;
            env(fset(bob, asfDepositAuth));
            env.close();

            auto const credIdx = createExpiringCredential(env, carol);

            // Advance ledger past credential expiration
            env.close(std::chrono::seconds(expireTime));

            // Fails with tecNO_PERMISSION.
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecNO_PERMISSION,
            });
            env.close();

            // Expired credential is not deleted
            BEAST_EXPECT(!credentialDeleted(env, carol));
        }
    }

    void
    testSendCredentialValidation(FeatureBitset features)
    {
        testcase("Send credential validation");
        using namespace test::jtx;

        // Tests for credentials::checkFields (preflight) and
        // credentials::valid (preclaim) validation.
        //
        // Preflight checks (temMALFORMED):
        //   - Empty credentials array
        //   - Array size exceeds maxCredentialsArraySize (8)
        //   - Duplicate credential IDs in array
        //
        // Preclaim checks (tecBAD_CREDENTIALS):
        //   - Credential doesn't exist
        //   - Credential doesn't belong to source account
        //   - Credential not accepted (lsfAccepted flag not set)

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dpIssuer("dpIssuer");
        char const credType[] = "KYC";

        // TEST 1: Preflight - Empty Credentials Array
        {
            Env env(*this, features);
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = std::vector<std::string>{},
                .err = temMALFORMED,
            });
        }

        // TEST 2: Preflight - Credentials Array Too Large
        {
            Env env(*this, features);
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            std::vector<std::string> tooManyCredentials;
            tooManyCredentials.reserve(9);
            for (int i = 0; i < 9; ++i)
                tooManyCredentials.push_back(to_string(uint256(i)));

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = tooManyCredentials,
                .err = temMALFORMED,
            });
        }

        // TEST 3: Preflight - Duplicate Credentials
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            env(credentials::create(carol, dpIssuer, credType));
            env.close();
            env(credentials::accept(carol, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, carol, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx, credIdx}},
                .err = temMALFORMED,
            });
        }

        // TEST 4: Preclaim - Credential Doesn't Exist
        {
            Env env(*this, features);
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            std::string const fakeCredIdx = to_string(uint256(999));
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{fakeCredIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }

        // TEST 5: Preclaim - Credential Doesn't Belong to Source Account
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            // Create credential for BOB (not carol)
            env(credentials::create(bob, dpIssuer, credType));
            env.close();
            env(credentials::accept(bob, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, bob, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }

        // TEST 6: Preclaim - Credential Not Accepted
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            // Create credential but DON'T accept it
            env(credentials::create(carol, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, carol, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }

        // TEST 7: Preflight - sfCredentialIDs requires featureCredentials.
        // Even with featureConfidentialTransfer enabled, supplying
        // CredentialIDs while featureCredentials is disabled must be
        // rejected in preflight via checkExtraFeatures.
        {
            Env env(*this, features - featureCredentials);
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50},
                 {.account = carol, .payAmount = 100, .convertAmount = 50}}};
            auto& mpt = confEnv.mpt;

            auto constexpr kCredIdx =
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288BE4";

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{kCredIdx}},
                .err = temDISABLED,
            });
        }
    }

    // Bob creates the AMM, but Bob is not the MPT holder checked below.
    // The AMM has its own pseudo-account (`ammHolder`) that can hold the
    // public MPT pool balance. That pseudo-account cannot normally
    // initialize confidential state because the confidential txn's must be
    // signed by sfAccount, and the AMM pseudo-account has no signing key.
    // So this is a construction/impossibility test: public AMM MPT state exists
    // but the corresponding confidential AMM clawback flow is not normally reachable.
    void
    testAMMHolderCannotHaveConfidentialStateClawback(FeatureBitset features)
    {
        testcase("AMM holder cannot have confidential state");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");

        for (bool const enablePseudoAccount : {false, true})
        {
            Env env{
                *this,
                enablePseudoAccount ? features | featureSingleAssetVault
                                    : features - featureSingleAssetVault};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .flags = kMptDexFlags | tfMPTCanClawback | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 1'000);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            AMM const amm(env, bob, XRP(100), mptAlice(100));
            Account const ammHolder("amm", amm.ammAccount());
            auto const ammSle = env.le(keylet::account(ammHolder.id()));

            BEAST_EXPECT(ammSle && ammSle->isFieldPresent(sfAMMID));
            BEAST_EXPECT(mptAlice.getBalance(ammHolder) == 100);

            BEAST_EXPECT(!mptAlice.getEncryptedBalance(ammHolder, MPTTester::holderEncryptedInbox));
            BEAST_EXPECT(
                !mptAlice.getEncryptedBalance(ammHolder, MPTTester::holderEncryptedSpending));
            BEAST_EXPECT(
                !mptAlice.getEncryptedBalance(ammHolder, MPTTester::issuerEncryptedBalance));
            BEAST_EXPECT(
                !mptAlice.getEncryptedBalance(ammHolder, MPTTester::auditorEncryptedBalance));

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = ammHolder,
                .amt = 100,
                .proof = strHex(gMakeZeroBuffer(kEcClawbackProofLength)),
                .err = tecNO_PERMISSION,
            });
        }
    }

    // Exercises every Confidential Transfer transaction type (MPTokenIssuanceSet,
    // Convert, MergeInbox, Send, ConvertBack) using tickets instead of regular account
    // sequence numbers.
    void
    testWithTickets(FeatureBitset features)
    {
        testcase("Confidential transfer with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        // MPTokenIssuanceSet with ticket, registers alice's issuer key.
        {
            std::uint32_t const ticketSeq = env.seq(alice) + 1;
            env(ticket::create(alice, 1));
            mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice), .ticketSeq = ticketSeq});
        }

        // ConfidentialMPTConvert with ticket, first convert registers bob's key.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
                .ticketSeq = ticketSeq,
            });
            env.require(MptBalance(mptAlice, bob, 50));
        }

        // ConfidentialMPTConvert with ticket
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.convert({.account = bob, .amt = 20, .ticketSeq = ticketSeq});
            env.require(MptBalance(mptAlice, bob, 30));
        }

        // ConfidentialMPTMergeInbox with ticket.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq});
        }

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // ConfidentialMPTSend with ticket.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .ticketSeq = ticketSeq});
        }

        // Merge carol's inbox so her spending balance includes the received send.
        mptAlice.mergeInbox({.account = carol});

        // ConfidentialMPTConvertBack with ticket.
        // The convertBack proof context hash must use the ticket sequence.
        {
            std::uint32_t const ticketSeq = env.seq(carol) + 1;
            env(ticket::create(carol, 1));
            mptAlice.convertBack({.account = carol, .amt = 10, .ticketSeq = ticketSeq});
            // carol converted 50, received 10 from bob, then converted back 10 → public 60
            env.require(MptBalance(mptAlice, carol, 60));
        }
    }

    // Verifies that cryptographic proofs in Convert transactions are bound to
    // the ticket sequence rather than the account sequence.
    // A proof built with the ticket sequence passes.
    void
    testConvertTicketProofBinding(FeatureBitset features)
    {
        testcase("Convert proof binds to ticket sequence");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
        mptAlice.generateKeyPair(bob);

        uint64_t const amt = 30;
        Buffer const bf = generateBlindingFactor();
        Buffer const holderCt = mptAlice.encryptAmount(bob, amt, bf);
        Buffer const issuerCt = mptAlice.encryptAmount(alice, amt, bf);

        std::uint32_t const ticketSeq1 = env.seq(bob) + 1;
        env(ticket::create(bob, 1));

        // Invalid: Schnorr proof built with the account seq (env.seq(bob)) rather
        // than the ticket seq (ticketSeq1).
        {
            BEAST_EXPECT(env.seq(bob) != ticketSeq1);
            uint256 const badCtxHash =
                getConvertContextHash(bob, mptAlice.issuanceID(), env.seq(bob));
            auto const badProof = requireOptional(
                mptAlice.getSchnorrProof(bob, badCtxHash), "Missing Schnorr Proof.");

            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .proof = strHex(badProof),
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .ticketSeq = ticketSeq1,
                .err = tecBAD_PROOF,
            });
        }

        std::uint32_t const ticketSeq2 = env.seq(bob) + 1;
        env(ticket::create(bob, 1));

        // Valid: proof auto-generated by convert() using ticketSeq2; context hashes match.
        mptAlice.convert({
            .account = bob,
            .amt = amt,
            .holderPubKey = mptAlice.getPubKey(bob),
            .holderEncryptedAmt = holderCt,
            .issuerEncryptedAmt = issuerCt,
            .blindingFactor = bf,
            .ticketSeq = ticketSeq2,
        });
        env.require(MptBalance(mptAlice, bob, 70));
    }

    // Exercises ticket-specific error codes for confidential transfer transactions:
    void
    testDestinationTag(FeatureBitset features)
    {
        testcase("test Destination Tag");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        // Set RequireDest on carol
        env(fset(carol, asfRequireDest));
        env.close();

        // Send without destination tag — rejected
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .senderEncryptedAmt = getTrivialCiphertext(),
            .destEncryptedAmt = getTrivialCiphertext(),
            .issuerEncryptedAmt = getTrivialCiphertext(),
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecDST_TAG_NEEDED,
        });

        // Send with destination tag — succeeds (passes preclaim,
        // reaches ZKP verification with the real proof)
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .destinationTag = 42});

        // Verify the destination tag is in the confirmed transaction
        auto const tx = env.tx();
        BEAST_EXPECT(tx);
        BEAST_EXPECT(tx->isFieldPresent(sfDestinationTag));
        BEAST_EXPECT((*tx)[sfDestinationTag] == 42);

        env(fclear(carol, asfRequireDest));
        env.close();

        // Send without destination tag when not required — succeeds
        mptAlice.mergeInbox({.account = carol});
        mptAlice.send({.account = bob, .dest = carol, .amt = 10});
    }

    // terPRE_TICKET when the ticket doesn't exist yet, and tefNO_TICKET when
    // the ticket has already been consumed or was never created.
    void
    testTicketErrors(FeatureBitset features)
    {
        testcase("Confidential transfer ticket errors");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
        mptAlice.generateKeyPair(bob);

        // Give bob an inbox balance so MergeInbox has something to merge.
        mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});

        // Use MergeInbox as the confidential transfer transaction under test
        // so that ticket errors are isolated from cryptographic verification.

        // terPRE_TICKET: ticket sequence is far in the future and hasn't been created.
        mptAlice.mergeInbox(
            {.account = bob, .ticketSeq = env.seq(bob) + 100, .err = terPRE_TICKET});

        // Create one ticket and use it successfully.
        std::uint32_t const ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq});

        // tefNO_TICKET: attempt to reuse the same (already-consumed) ticket.
        mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq, .err = tefNO_TICKET});

        // tefNO_TICKET: ticket sequence is in the past but was never created.
        mptAlice.mergeInbox({.account = bob, .ticketSeq = 1, .err = tefNO_TICKET});
    }

    // Bob sends 100 MPT to Carol. Carol Merge Inbox. Carol sends 50 MPT to Dave.
    // Inner 3rd txn (Carol sends to Dave) fails because the proof is built with
    // when Carols's spending balance is 0. (before she received funds from Bob)
    //
    // Also tests Bob sending to two recipients (Carol and Dave) in a single
    // batch. Even though Bob has enough balance for both, the second send's
    // balance-linkage proof becomes incorrect once inner 1 updates Bob's encrypted
    // spending, so fails
    void
    testBatchConfidentialSend(FeatureBitset features)
    {
        testcase("Batch confidential send - merge inbox dependency");
        using namespace test::jtx;

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob = A (100 spending), carol = B (0), dave = C (0)
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Build the batch:
            //   Batch Txn 1 bob -> carol 100 : valid proof, bob spending=100
            //   Batch Txn 2 carol -> mergeInbox : valid JV
            //   Batch Txn 3 carol->dave 50  : Invalid
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            // 3 signers, Bob, Carol, Dave
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 3);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 100}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = carol});
            auto const jv3 = mpt.sendJV({.account = carol, .dest = dave, .amt = 50}, carolSeq + 1);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Inner(jv3, carolSeq + 1),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // AllOrNothing: inner 3 fails
            // bob's spending must remain 100; carol's inbox must remain 0.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
        }

        // Bob sends to two recipients (Carol and Dave) in one batch.
        // Bob has 150, enough for both sends individually.  However, batch txn 1
        // changes Bob's encrypted spending on the ledger; batch txn 2 was built
        // against the old enc(150) so its balance-linkage proof is stale.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 150, 0);

            // tfAllOrNothing — rejects the whole batch as 2nd txn proof is incorrect
            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

                auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
                auto const jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 60}, bobSeq + 2);

                env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                    batch::Inner(jv1, bobSeq + 1),
                    batch::Inner(jv2, bobSeq + 2),
                    Ter(tesSUCCESS));
                env.close();

                // Nothing applied: bob stays 150, carol and dave inbox stay 0.
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 150);
                BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
                BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
            }

            // If we change batch mode to be tfIndependent — txn 1 applies, inner 2 fails.
            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

                auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
                auto const jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 60}, bobSeq + 2);

                env(batch::outer(bob, bobSeq, batchFee, tfIndependent),
                    batch::Inner(jv1, bobSeq + 1),
                    batch::Inner(jv2, bobSeq + 2),
                    Ter(tesSUCCESS));
                env.close();

                // bob 150→100, carol inbox 0→50
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
                BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 50);
                // dave gets nothing
                BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
            }
        }

        // Now, Bob sends Confidential MPT to 2 accounts in one batch.
        // However this time, the second txn proof is calculated using the
        // correct encrypted(spending) proof, so it should pass.
        {
            // bob has exactly enough for both sends.
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 200, 0);

            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

                // jv1 is built against the current ledger state (spending=200).
                auto const jv1 =
                    mpt.sendJV({.account = bob, .dest = carol, .amt = 100}, bobSeq + 1);

                // Compute post-jv1 state without touching the ledger.
                auto const chain1 = mpt.chainAfterSend(bob, 100, jv1);

                // jv2 proof is built against predicted spending=100, version=N+1.
                auto const jv2 =
                    mpt.sendJV({.account = bob, .dest = dave, .amt = 100}, bobSeq + 2, chain1);

                env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                    batch::Inner(jv1, bobSeq + 1),
                    batch::Inner(jv2, bobSeq + 2),
                    Ter(tesSUCCESS));
                env.close();

                // Both txns applied: bob 200→0, carol inbox=100, dave inbox=100.
                BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 0);
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 100);
                BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 100);
            }

            // Now Bob has 150, but tries to send two 100 in one batch.
            // This fails because Bob doesn't have enough MPT balance.
            {
                Env env2{*this, features};
                Account const alice2("alice");
                Account const bob2("bob");
                Account const carol2("carol");
                Account const dave2("dave");

                MPTTester mpt2(env2, alice2, {.holders = {bob2, carol2, dave2}});
                setupBatchEnv(mpt2, alice2, bob2, carol2, dave2, 150, 0);

                auto const bobSeq = env2.seq(bob2);
                auto const batchFee = batch::calcConfidentialBatchFee(env2, 0, 2);

                auto const jv1 =
                    mpt2.sendJV({.account = bob2, .dest = carol2, .amt = 100}, bobSeq + 1);
                auto const chain1 = mpt2.chainAfterSend(bob2, 100, jv1);

                auto const jv2 =
                    mpt2.sendJV({.account = bob2, .dest = dave2, .amt = 100}, bobSeq + 2, chain1);

                env2(
                    batch::outer(bob2, bobSeq, batchFee, tfAllOrNothing),
                    batch::Inner(jv1, bobSeq + 1),
                    batch::Inner(jv2, bobSeq + 2),
                    Ter(tesSUCCESS));
                env2.close();

                // AllOrNothing: inner 2 fails → nothing applied.
                BEAST_EXPECT(
                    mpt2.getDecryptedBalance(bob2, MPTTester::holderEncryptedSpending) == 150);
                BEAST_EXPECT(
                    mpt2.getDecryptedBalance(carol2, MPTTester::holderEncryptedInbox) == 0);
                BEAST_EXPECT(mpt2.getDecryptedBalance(dave2, MPTTester::holderEncryptedInbox) == 0);
            }
        }
    }
    void
    testBatchConfidentialConvertAndConvertBack(FeatureBitset features)
    {
        testcase("Batch confidential convert and convertBack");
        using namespace test::jtx;

        // convert + convertBack in one AllOrNothing batch, both valid.
        //
        // Bob has regular=50, spending=100.
        // jv1: convert 50 regular → inbox  (Schnorr proof; does NOT touch spending/version)
        // jv2: convertBack 30 spending → regular  (proof against spending=100, version=V)
        //
        // Since jv1 leaves spending and version unchanged, jv2's proof is still
        // valid when it executes, so both inner txns succeed.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob: spending=100, regular=0 after setupBatchEnv;
            // pay 50 more to give bob regular MPT to convert in the batch.
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            mpt.pay(alice, bob, 50);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // jv1: convert 50 regular MPT into confidential inbox
            auto const jv1 = mpt.convertJV({.account = bob, .amt = 50}, bobSeq + 1);
            // jv2: convert 30 spending back to regular MPT
            auto const jv2 = mpt.convertBackJV({.account = bob, .amt = 30}, bobSeq + 2);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, bobSeq + 2),
                Ter(tesSUCCESS));
            env.close();

            //   regular (mptAmount): 50 (pre) - 50 (convert) + 30 (convertBack) = 30
            //   spending balance: 100 - 30 = 70
            //   inbox:    0   + 50 (from convert) = 50
            env.require(MptBalance(mpt, bob, 30));
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 70);
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == 50);
        }

        // convert + mergeInbox + convertBack, stale convertBack proof.
        //
        // jv1: convert 50 regular → inbox
        // jv2: mergeInbox (inbox 50 → spending, version V → V+1)
        // jv3: convertBack 30 (proof built against spending=100, version=V)
        //
        // After jv2 applies, spending=150 and version=V+1, so jv3's
        // proof is stale.  AllOrNothing rejects the whole batch.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            mpt.pay(alice, bob, 50);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 3);

            auto const jv1 = mpt.convertJV({.account = bob, .amt = 50}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = bob});
            // jv3 proof is built against spending=100, version=V (pre-batch)
            auto const jv3 = mpt.convertBackJV({.account = bob, .amt = 30}, bobSeq + 3);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, bobSeq + 2),
                batch::Inner(jv3, bobSeq + 3),
                Ter(tesSUCCESS));
            env.close();

            // jv3 fails so nothing is applied.
            env.require(MptBalance(mpt, bob, 50));
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == 0);
        }
    }

    // Tests a batch containing all four confidential MPT operations, Send,
    // Convert, ConvertBack, and MergeInbox in a single AllOrNothing batch.
    void
    testBatchConfidentialMixTransactions(FeatureBitset features)
    {
        testcase("Batch confidential mixed operations");
        using namespace test::jtx;

        // send(bob→carol) + convert(carol) + convertBack(dave)
        //  + mergeInbox(carol) in one AllOrNothing batch.
        //
        // Setup:
        //   bob:   spending=100, regular=0
        //   carol: spending=0,   regular=50
        //   dave:  spending=50,  regular=0
        //
        // After the batch:
        //   bob   spending: 100 -> 70  (sent 30 to carol)
        //   carol inbox:    0+30(send)+50(convert)=80 -> merged -> spending=80, inbox=0
        //   dave  spending: 50 -> 30; regular: 0 -> 20
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob: spending=100. carol: key registered, spending=0.
            // dave: key registered, spending=0 initially.
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            // Give carol 50 regular MPT to convert in the batch.
            mpt.pay(alice, carol, 50);
            // Give dave 50 regular MPT then convert to confidential spending.
            mpt.pay(alice, dave, 50);
            mpt.convert({.account = dave, .amt = 50});
            mpt.mergeInbox({.account = dave});

            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const daveSeq = env.seq(dave);
            // 2 extra signers (carol, dave), 4 inner txns
            auto const batchFee = batch::calcConfidentialBatchFee(env, 2, 4);

            // jv1: bob sends 30 to carol
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 30}, bobSeq + 1);
            // jv2: carol converts her 50 regular MPT to confidential
            auto const jv2 = mpt.convertJV({.account = carol, .amt = 50}, carolSeq);
            // jv3: dave converts 20 spending back to regular MPT
            auto const jv3 = mpt.convertBackJV({.account = dave, .amt = 20}, daveSeq);
            // jv4: carol merges inbox into spending
            //   (inbox = 30 from jv1 + 50 from jv2 = 80 at execution time)
            auto const jv4 = mpt.mergeInboxJV({.account = carol});

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Inner(jv3, daveSeq),
                batch::Inner(jv4, carolSeq + 1),
                batch::Sig(carol, dave),
                Ter(tesSUCCESS));
            env.close();

            // All four applied:
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 70);
            // carol's inbox was merged: spending=80, inbox=0
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 80);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
            // dave: spending=30, regular=20
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedSpending) == 30);
            env.require(MptBalance(mpt, dave, 20));
        }

        // bob send + bob convertBack in one AllOrNothing batch.
        //
        // The Send applies first and increments Bob's version counter.
        // The ConvertBack proof was built against the pre-Send (spending=100,
        // version=V), so batch txn is rejected.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // jv1: bob sends 30 to carol (spending 100->70, version V->V+1)
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 30}, bobSeq + 1);
            // jv2: bob convertBack 40 , proof built against spending=100, version=V
            auto const jv2 = mpt.convertBackJV({.account = bob, .amt = 40}, bobSeq + 2);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, bobSeq + 2),
                Ter(tesSUCCESS));
            env.close();

            // AllOrNothing: jv2 fails (stale proof) → nothing applied.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
        }
    }

    // Verifies that batch transactions work correctly when tickets are used instead
    // of sequence numbers
    void
    testBatchAllOrNothing(FeatureBitset features)
    {
        testcase("Batch confidential MPT - all or nothing");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // bob sends dave 10, carol sends dave 5, independent, both valid.
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // Both txn applied: bob's balance 100→90, carol 60→55, dave inbox 0→15
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 90);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 15);
        }
    }

    void
    testBatchOnlyOne(FeatureBitset features)
    {
        testcase("Batch confidential MPT - only one");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // bob sends dave 200 (invalid), carol sends dave 300 (invalid)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            // Both proofs fail range check (amount > balance)
            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 300}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfOnlyOne),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // No success found → nothing applied; balances unchanged
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 60);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
        }

        // bob sends dave 200 (invalid), carol sends dave 5 (valid)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfOnlyOne),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // Only carol's send applied: carol 60→55, dave inbox 0→5, bob unchanged
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 5);
        }
    }

    void
    testBatchUntilFailure(FeatureBitset features)
    {
        testcase("Batch confidential MPT - until failure");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // first fails → none applied
        // Bob sends Dave 200 (invalid — stops immediately)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfUntilFailure),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 60);
        }

        // Bob sends dave 10, Carol sends dave 5 — both valid and independent
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfUntilFailure),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // Both applied: bob 100→90, carol 60→55, dave inbox 0→15
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 90);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 15);
        }
    }

    void
    testBatchIndependent(FeatureBitset features)
    {
        testcase("Batch confidential MPT - independent");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // Bob sends dave 10 (valid), Carol sends dave 300
        // (invalid), Carol sends Dave 5 (valid). Carol's
        // balance is still 60 because the preceding send failed).
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 3);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);

            // Carol trying to send dave 300 but own balance only 60
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 300}, carolSeq);
            auto const jv3 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq + 1);

            env(batch::outer(bob, bobSeq, batchFee, tfIndependent),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Inner(jv3, carolSeq + 1),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // inner 1 (bob→dave 10) applied: bob 100→90
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 90);
            // inner 2 failed (carol not changed), inner 3 applied: carol 60→55
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 55);
            // dave inbox: 10 (from bob) + 5 (from carol inner 3) = 15
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 15);
        }
    }

    // Tests batching ConfidentialMPTConvert and a ConfidentialMPTConvertBack
    // in the same batch transaction. Because Convert only modifies the inbox
    // (never the spending balance or the version counter), a ConvertBack proof
    // built against the pre-batch spending balance is still valid when both
    // appear in the same batch.
    void
    testBatchWithTickets(FeatureBitset features)
    {
        testcase("Batch confidential MPT with tickets");
        using namespace test::jtx;

        // outer batch uses a ticket.
        // The inner send proofs are still bound to regular account sequences.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Bob creates one ticket to use for the outer batch.
            std::uint32_t const outerTicketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            env.close();

            auto const bobSeq = env.seq(bob);
            // 0 extra signers: all inner txns are from bob;
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // When the outer uses a ticket (seq=0), inner txns start from bobSeq, bobSeq+1.
            // jv2 must use chain state predicted after jv1 since both sends are from bob.
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq);
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            auto const jv2 =
                mpt.sendJV({.account = bob, .dest = dave, .amt = 20}, bobSeq + 1, chain1);

            env(batch::outer(bob, 0, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq),
                batch::Inner(jv2, bobSeq + 1),
                ticket::Use(outerTicketSeq),
                Ter(tesSUCCESS));
            env.close();

            // Both sends applied: bob 100→40, carol inbox=40, dave inbox=20.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 20);
        }

        // inner transactions each consume their own ticket.
        // The send proof context hash must be bound to the ticket sequence, not the
        // account sequence. sendJV receives the ticket seq as its `seq` parameter.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Bob creates two tickets for the two inner sends.
            std::uint32_t const ticketSeq1 = env.seq(bob) + 1;
            std::uint32_t const ticketSeq2 = env.seq(bob) + 2;
            env(ticket::create(bob, 2));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // jv1: proof bound to ticketSeq1.
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, ticketSeq1);
            // jv2: proof bound to ticketSeq2, spending state predicted after jv1.
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            auto const jv2 =
                mpt.sendJV({.account = bob, .dest = dave, .amt = 30}, ticketSeq2, chain1);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, 0, ticketSeq1),
                batch::Inner(jv2, 0, ticketSeq2),
                Ter(tesSUCCESS));
            env.close();

            // Both sends applied: bob 100→30, carol inbox=40, dave inbox=30.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 30);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 30);
        }

        // inner send uses wrong sequence (account seq instead of ticket seq)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // Proof intentionally built with account seq (bobSeq+1) instead of ticketSeq.
            auto const badJV = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = bob});

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(badJV, 0, ticketSeq),
                batch::Inner(jv2, bobSeq + 1),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
        }
    }

    // Basic tests of confidential transfer through delegation. Verifies that a delegated account
    // with the appropriate permissions can execute confidential transfer transactions
    // on behalf of the delegator.
    void
    testConfidentialDelegation(FeatureBitset features)
    {
        testcase("Confidential transfers through delegation");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};

        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 200);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob delegates Convert, MergeInbox to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox"}));
        env.close();

        // Carol has no permission from bob to convert on his behalf.
        mptAlice.convert({
            .account = bob,
            .amt = 10,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = carol,
            .err = terNO_DELEGATE_PERMISSION,
        });

        // Dave executes Convert on behalf of bob, registering bob's key.
        mptAlice.convert({
            .account = bob,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = dave,
        });
        env.require(MptBalance(mptAlice, bob, 100));

        // Dave executes Convert again on behalf of bob (no key registration).
        mptAlice.convert({.account = bob, .amt = 50, .delegate = dave});

        // Dave executes MergeInbox on behalf of bob.
        mptAlice.mergeInbox({.account = bob, .delegate = dave});

        // Carol converts and merge inbox.
        mptAlice.convert({
            .account = carol,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Dave does not have permission to send on behalf of bob.
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = 10,
             .delegate = dave,
             .err = terNO_DELEGATE_PERMISSION});

        // Bob delegates ConfidentialMPTSend to dave.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox", "ConfidentialMPTSend"}));
        env.close();

        // Dave executes Send on behalf of bob.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = dave});
        mptAlice.mergeInbox({.account = carol});

        // Dave does not have permission to convert back on behalf of bob.
        mptAlice.convertBack(
            {.account = bob, .amt = 10, .delegate = dave, .err = terNO_DELEGATE_PERMISSION});

        // Bob delegates ConfidentialMPTConvertBack to dave.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTSend",
             "ConfidentialMPTConvertBack"}));
        env.close();

        // Dave executes ConvertBack on behalf of bob.
        mptAlice.convertBack({.account = bob, .amt = 10, .delegate = dave});

        // Dave does not have permission to clawback on behalf of alice.
        mptAlice.confidentialClaw(
            {.holder = bob, .amt = 130, .delegate = dave, .err = terNO_DELEGATE_PERMISSION});

        // Alice delegates ConfidentialMPTClawback to dave.
        env(delegate::set(alice, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave executes Clawback on behalf of alice.
        mptAlice.confidentialClaw({.holder = bob, .amt = 130, .delegate = dave});
    }

    // Verifies that revoking delegation prevents further delegated operations.
    void
    testDelegationRevocation(FeatureBitset features)
    {
        testcase("Confidential delegation revocation");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        env.fund(XRP(10000), carol);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Creating the Delegate SLE consumes one owner reserve slot for bob.
        auto const bobOwnersBefore = ownerCount(env, bob);
        env(delegate::set(bob, carol, {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox"}));
        env.close();
        env.require(Owners(bob, bobOwnersBefore + 1));

        // Carol converts and merge inbox on behalf of bob.
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = carol,
        });
        mptAlice.mergeInbox({.account = bob, .delegate = carol});

        // Bob revokes all permissions, deletes the Delegate SLE, releasing the reserve.
        env(delegate::set(bob, carol, std::vector<std::string>{}));
        env.close();
        env.require(Owners(bob, bobOwnersBefore));

        // Carol can no longer convert on behalf of bob.
        mptAlice.convert({
            .account = bob,
            .amt = 30,
            .delegate = carol,
            .err = terNO_DELEGATE_PERMISSION,
        });

        // Bob can still convert by himself.
        mptAlice.convert({.account = bob, .amt = 30});
    }

    // Verifies that a delegated confidential transfer works correctly when an
    // auditor is configured on the issuance.
    void
    testDelegationWithAuditor(FeatureBitset features)
    {
        testcase("Confidential delegation with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const auditor{"auditor"};

        MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(auditor);
        mptAlice.set({
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(auditor),
        });

        // Bob delegates Convert and Send permissions to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTSend", "ConfidentialMPTConvert"}));
        env.close();

        // Dave converts on behalf of bob.
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = dave,
        });
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({
            .account = carol,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Dave sends on behalf of bob.
        mptAlice.send({.account = bob, .dest = carol, .amt = 20, .delegate = dave});
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = dave});

        // Bob delegates ConvertBack and Send permissions to auditor.
        env(delegate::set(bob, auditor, {"ConfidentialMPTSend", "ConfidentialMPTConvertBack"}));
        env.close();

        // auditor can send and convert back on behalf of bob as well.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = auditor});
        mptAlice.convertBack({.account = bob, .amt = 10, .delegate = auditor});
    }

    // Verifies that a non-issuer delegating clawback to a third party does not
    // allow that party to execute clawback, since clawback is issuer-only.
    void
    testDelegationClawbackIssuerOnly(FeatureBitset features)
    {
        testcase("Confidential clawback delegation requires issuer");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};

        ConfidentialEnv const confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 50},
             {.account = carol, .payAmount = 100, .convertAmount = 100}},
            tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;
        env.fund(XRP(10000), dave);
        env.close();

        // Bob delegates Clawback permission to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave attempts clawback on behalf of bob targetting bob, but since bob is not the issuer,
        // the transaction should be rejected.
        {
            json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfHolder] = bob.human();
            jv[sfMPTAmount.jsonName] = "50";
            jv[sfZKProof.jsonName] = std::string(kEcClawbackProofLength * 2, '0');
            env(jv, delegate::As(dave), Ter(temMALFORMED));
        }

        // Dave attempts clawback on behalf of bob targeting carol, but since bob is not the issuer,
        // the transaction should be rejected.
        {
            json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfHolder] = carol.human();
            jv[sfMPTAmount.jsonName] = "100";
            jv[sfZKProof.jsonName] = std::string(kEcClawbackProofLength * 2, '0');
            env(jv, delegate::As(dave), Ter(temMALFORMED));
        }
    }

    // Batch with delegated ConfidentialMPTSend txs, covering stale and updated inner
    // send proofs.
    void
    testBatchDelegatedSend(FeatureBitset features)
    {
        testcase("Batch ConfidentialMPTSend with delegation");
        using namespace test::jtx;

        // AllOrNothing: two delegated sends from bob via dave, second proof is
        // stale once the first send updates bob's spending, whole batch rolls back.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            env(delegate::set(bob, dave, {"ConfidentialMPTSend"}));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // jv1: proof against spending balance 100
            auto jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 60}, bobSeq + 1);
            jv1[jss::Delegate] = dave.human();
            // jv2: proof also against spending balance 100, which is stale once jv1 applies
            auto jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 60}, bobSeq + 2);
            jv2[jss::Delegate] = dave.human();

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, bobSeq + 2),
                Ter(tesSUCCESS));
            env.close();

            // Stale proof on jv2, AllOrNothing rolls back everything.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
        }

        // AllOrNothing: two delegated sends with correctly chained proofs both apply.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            env(delegate::set(bob, dave, {"ConfidentialMPTSend"}));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            // jv1: proof against spending balance 100.
            auto jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq + 1);
            jv1[jss::Delegate] = dave.human();
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            // jv2: proof against predicted spending balance 60.
            auto jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 40}, bobSeq + 2, chain1);
            jv2[jss::Delegate] = dave.human();

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, bobSeq + 2),
                Ter(tesSUCCESS));
            env.close();

            // Both inner tx applied
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 20);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 40);
        }
    }

    // Test missing delegation permission inside a batch.
    void
    testBatchDelegationMissingPermission(FeatureBitset features)
    {
        testcase("Batch delegation missing permission");
        using namespace test::jtx;

        // AllOrNothing: dave has no Send permission from bob, so the delegated
        // inner send fails. The whole batch rolls back.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

            // Bob grants dave only MergeInbox, not Send.
            env(delegate::set(bob, dave, {"ConfidentialMPTMergeInbox"}));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            // jv1: direct send from carol (valid proof).
            auto const jv1 = mpt.sendJV({.account = carol, .dest = dave, .amt = 30}, carolSeq);
            // jv2: delegated send, fails because dave has no Send permission.
            auto jv2 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
            jv2[jss::Delegate] = dave.human();

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, carolSeq),
                batch::Inner(jv2, bobSeq + 1),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // jv1 applied in the batch view, then jv2 failed, so
            // AllOrNothing discards both inner effects.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 60);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
        }

        // Independent: the delegated confidential send is skipped because lack of permission. The
        // send from carol still applies.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

            // Bob does not grant dave any permissions.
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
            jv1[jss::Delegate] = dave.human();
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 30}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfIndependent),
                batch::Inner(jv1, bobSeq + 1),
                batch::Inner(jv2, carolSeq),
                batch::Sig(carol),
                Ter(tesSUCCESS));
            env.close();

            // jv1 failed and jv2 applied.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 30);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 30);
        }
    }

    // Test batch outer signer is the delegated account.
    void
    testBatchDelegatedSendWithDelegateAsOuterAccount(FeatureBitset features)
    {
        testcase("Test batch delegated send with delegate as outer account");
        using namespace test::jtx;

        // Dave has delegation permission, but the inner Account is bob.
        // Without bob's BatchSigner, the batch is rejected.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            env(delegate::set(bob, dave, {"ConfidentialMPTSend"}));
            env.close();

            auto const daveSeq = env.seq(dave);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 0, 2);

            auto jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq);
            jv1[jss::Delegate] = dave.human();
            auto const jv2 = mpt.mergeInboxJV({.account = dave});

            env(batch::outer(dave, daveSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq),
                batch::Inner(jv2, daveSeq + 1),
                Ter(temBAD_SIGNER));
            env.close();

            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
        }

        // Dave submits a mixed batch: bob signs inner tx1, and
        // dave is the Delegate account signing for inner tx2.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            env(delegate::set(bob, dave, {"ConfidentialMPTSend"}));
            env.close();

            auto const daveSeq = env.seq(dave);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq);
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            auto jv2 = mpt.sendJV({.account = bob, .dest = carol, .amt = 30}, bobSeq + 1, chain1);
            jv2[jss::Delegate] = dave.human();

            // Dave is outer; bob signs because his account appears in inner txns.
            env(batch::outer(dave, daveSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq),
                batch::Inner(jv2, bobSeq + 1),
                batch::Sig(bob),
                Ter(tesSUCCESS));
            env.close();

            // Both sends applied: bob 100→30, carol inbox=70.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 30);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 70);
        }

        // Verify the delegator Bob's BatchSigner does not bypass the missing delegation permission.
        // The delegated inner send fails.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

            // Bob does not grant dave any permissions.
            auto const daveSeq = env.seq(dave);
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcConfidentialBatchFee(env, 2, 2);

            auto jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq);
            jv1[jss::Delegate] = dave.human();
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 30}, carolSeq);

            env(batch::outer(dave, daveSeq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, bobSeq),
                batch::Inner(jv2, carolSeq),
                batch::Sig(bob, carol),
                Ter(tesSUCCESS));
            env.close();

            // jv1 fails before jv2 is attempted.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 60);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
        }
    }

    // Mixed batch with delegated and non-delegated inner confidential MPT transactions.
    void
    testBatchDelegatedConfidentialMix(FeatureBitset features)
    {
        testcase("Batch delegated confidential multiple operations");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const erin("erin");
        Account const frank("frank");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave, frank}});
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);
        mpt.pay(alice, bob, 50);
        env.fund(XRP(10000), erin);
        env.close();

        mpt.authorize({.account = frank});
        mpt.pay(alice, frank, 40);
        mpt.generateKeyPair(frank);

        env(delegate::set(bob, dave, {"ConfidentialMPTConvert", "ConfidentialMPTConvertBack"}));
        env(delegate::set(carol, erin, {"ConfidentialMPTSend"}));
        env(delegate::set(bob, erin, {"ConfidentialMPTMergeInbox"}));
        env.close();

        auto const daveSeq = env.seq(dave);
        auto const bobSeq = env.seq(bob);
        auto const carolSeq = env.seq(carol);
        auto const frankSeq = env.seq(frank);
        auto const batchFee = batch::calcConfidentialBatchFee(env, 3, 6);

        // Dave submits the batch. Bob's convert and convertback use Dave as Delegate;
        // Carol's send and Bob's mergeInbox use Erin as Delegate. Frank's
        // convert and mergeInbox are non-delegated.
        auto jv1 = mpt.convertBackJV({.account = bob, .amt = 30}, bobSeq);
        jv1[jss::Delegate] = dave.human();
        auto jv2 = mpt.convertJV({.account = bob, .amt = 20}, bobSeq + 1);
        jv2[jss::Delegate] = dave.human();
        auto jv3 = mpt.sendJV({.account = carol, .dest = bob, .amt = 15}, carolSeq);
        jv3[jss::Delegate] = erin.human();
        auto const jv4 = mpt.convertJV(
            {.account = frank, .amt = 25, .holderPubKey = mpt.getPubKey(frank)}, frankSeq);
        auto const jv5 = mpt.mergeInboxJV({.account = frank});
        auto jv6 = mpt.mergeInboxJV({.account = bob});
        jv6[jss::Delegate] = erin.human();

        env(batch::outer(dave, daveSeq, batchFee, tfAllOrNothing),
            batch::Inner(jv1, bobSeq),
            batch::Inner(jv2, bobSeq + 1),
            batch::Inner(jv3, carolSeq),
            batch::Inner(jv4, frankSeq),
            batch::Inner(jv5, frankSeq + 1),
            batch::Inner(jv6, bobSeq + 2),
            batch::Sig(bob, carol, frank),
            Ter(tesSUCCESS));
        env.close();

        env.require(MptBalance(mpt, bob, 60));
        BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 105);
        BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == 0);
        env.require(MptBalance(mpt, carol, 0));
        BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedSpending) == 45);
        BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
        env.require(MptBalance(mpt, frank, 15));
        BEAST_EXPECT(mpt.getDecryptedBalance(frank, MPTTester::holderEncryptedSpending) == 25);
        BEAST_EXPECT(mpt.getDecryptedBalance(frank, MPTTester::holderEncryptedInbox) == 0);
        env.require(MptBalance(mpt, dave, 0));
        BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedSpending) == 0);
        BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::holderEncryptedInbox) == 0);
        auto const outstandingBalance = mpt.getIssuanceOutstandingBalance();
        BEAST_EXPECT(outstandingBalance && *outstandingBalance == 250);
        BEAST_EXPECT(mpt.getIssuanceConfidentialBalance() == 175);
    }

    // Test invalid scenarios for delegation with tickets.
    void
    testInvalidDelegationWithTickets(FeatureBitset features)
    {
        testcase("Invalid cases for delegation with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        env.fund(XRP(10000), carol);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance | tfMPTCanClawback,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 200);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob grants carol permissions.
        env(delegate::set(bob, carol, {"ConfidentialMPTConvert"}));
        env.close();

        uint64_t const amt = 10;
        auto const bf = generateBlindingFactor();
        auto const holderCt = mptAlice.encryptAmount(bob, amt, bf);
        auto const issuerCt = mptAlice.encryptAmount(alice, amt, bf);

        // Invalid: proof built with wrong ticket sequence (ticketSeq + 1).
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));

            auto const badCtxHash =
                getConvertContextHash(bob, mptAlice.issuanceID(), ticketSeq + 1);
            auto const badProof = requireOptional(
                mptAlice.getSchnorrProof(bob, badCtxHash), "Missing Schnorr Proof.");

            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .proof = strHex(badProof),
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = ticketSeq,
                .err = tecBAD_PROOF,
            });
        }

        // Invalid: proof built with account sequence instead of ticket sequence.
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            auto const badCtxHash = getConvertContextHash(bob, mptAlice.issuanceID(), env.seq(bob));
            auto const badProof = requireOptional(
                mptAlice.getSchnorrProof(bob, badCtxHash), "Missing Schnorr Proof.");

            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .proof = strHex(badProof),
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = ticketSeq,
                .err = tecBAD_PROOF,
            });
        }

        // Invalid: ticket sequence is far in the future and hasn't been created yet.
        {
            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = env.seq(bob) + 100,
                .err = terPRE_TICKET,
            });
        }

        // Invalid: ticket sequence is in the past but was never created.
        {
            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = 1,
                .err = tefNO_TICKET,
            });
        }

        // Invalid: the delegated account, carol, creates a ticket and uses it.
        {
            auto const carolTicketSeq = env.seq(carol) + 1;
            env(ticket::create(carol, 1));

            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = carolTicketSeq,
                .err = tefNO_TICKET,
            });
        }

        // Invalid: proof bound to a ticket sequence but submitted without a ticket,
        // using account sequence.
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));

            // Build proof using ticketSeq.
            auto const ctxHashForTicket =
                getConvertContextHash(bob, mptAlice.issuanceID(), ticketSeq);
            auto const proof = requireOptional(
                mptAlice.getSchnorrProof(bob, ctxHashForTicket), "Missing Schnorr Proof.");

            // Submit without ticket.
            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .proof = strHex(proof),
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .err = tecBAD_PROOF,
            });
        }
    }

    // Verifies that delegation works correctly when the delegating account uses
    // tickets instead of regular sequence numbers. The proof must bind to the
    // ticket sequence, not the account sequence.
    void
    testDelegationWithTickets(FeatureBitset features)
    {
        testcase("Confidential delegation with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance | tfMPTCanClawback,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 200);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob grants dave permissions.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTSend",
             "ConfidentialMPTConvertBack"}));
        // Alice grants dave permission to clawback on her behalf.
        env(delegate::set(alice, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave executes Convert on behalf of bob using ticket.
        auto ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.convert({
            .account = bob,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });
        env.require(MptBalance(mptAlice, bob, 100));

        // MergeInbox using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.mergeInbox({.account = bob, .delegate = dave, .ticketSeq = ticketSeq});

        // Carol converts and merges inbox to receive from bob.
        mptAlice.convert({
            .account = carol,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Send using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 20,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });

        // ConvertBack using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.convertBack({
            .account = bob,
            .amt = 10,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });

        // Clawback using ticket with delegation.
        ticketSeq = env.seq(alice) + 1;
        env(ticket::create(alice, 1));
        BEAST_EXPECT(env.seq(alice) != ticketSeq);
        mptAlice.confidentialClaw({
            .holder = bob,
            .amt = 70,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // DepositAuth, credentials, and destination tag interactions.
        testSendDepositPreauth(features);
        testSendCredentialValidation(features);
        testDestinationTag(features);

        // AMM/pseudo-account interaction.
        testAMMHolderCannotHaveConfidentialStateClawback(features);

        // Ticket interactions.
        testWithTickets(features);
        testConvertTicketProofBinding(features);
        testTicketErrors(features);

        // Batch interactions.
        testBatchConfidentialSend(features);
        testBatchConfidentialConvertAndConvertBack(features);
        testBatchConfidentialMixTransactions(features);
        testBatchAllOrNothing(features);
        testBatchOnlyOne(features);
        testBatchUntilFailure(features);
        testBatchIndependent(features);
        testBatchWithTickets(features);

        // Permission delegation interactions.
        testConfidentialDelegation(features);
        testDelegationRevocation(features);
        testDelegationWithAuditor(features);
        testDelegationClawbackIssuerOnly(features);
        testBatchDelegatedSend(features);
        testBatchDelegationMissingPermission(features);
        testBatchDelegatedSendWithDelegateAsOuterAccount(features);
        testBatchDelegatedConfidentialMix(features);
        testInvalidDelegationWithTickets(features);
        testDelegationWithTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialTransferExtended, app, xrpl);

}  // namespace xrpl
