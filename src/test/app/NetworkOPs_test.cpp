#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>
#include <test/jtx/utility.h>

#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Manifest.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/asio/ip/host_name.hpp>

#include <memory>

namespace xrpl::test {

class NetworkOPs_test : public beast::unit_test::suite
{
    struct TestInfoSub : InfoSub
    {
        Json::Value last;
        bool sentBroadcast = false;

        explicit TestInfoSub(Source& source) : InfoSub(source)
        {
            setApiVersion(1);
        }

        void
        send(Json::Value const& jvObj, bool broadcast) override
        {
            last = jvObj;
            sentBroadcast = broadcast;
        }
    };

    static uint256
    digest(std::string const& value)
    {
        return sha512Half(value);
    }

    static std::shared_ptr<STTx const>
    makeInnerBatchTx(
        jtx::Env& env,
        jtx::Account const& from,
        jtx::Account const& to,
        STAmount const& amount,
        std::uint32_t seqValue)
    {
        auto jt = env.jt(jtx::pay(from, to, amount), jtx::seq(seqValue));
        jt[jss::Flags] = tfInnerBatchTxn;
        auto obj = jtx::parse(jt.jv);
        return std::make_shared<STTx const>(std::move(obj));
    }

    static Manifest
    makeManifest()
    {
        auto const master = randomKeyPair(KeyType::ed25519);
        auto const signing = randomKeyPair(KeyType::secp256k1);

        STObject st(sfGeneric);
        st[sfSequence] = 7;
        st[sfPublicKey] = master.first;
        st[sfSigningPubKey] = signing.first;
        std::string const domain = "example.com";
        st[sfDomain] = makeSlice(domain);

        sign(st, HashPrefix::manifest, KeyType::ed25519, master.second, sfMasterSignature);
        sign(st, HashPrefix::manifest, KeyType::secp256k1, signing.second);

        Serializer s;
        st.add(s);

        std::string const serialized(static_cast<char const*>(s.data()), s.size());
        auto manifest = deserializeManifest(serialized);
        XRPL_ASSERT(manifest, "xrpl::test::NetworkOPs_test valid manifest");
        return std::move(*manifest);
    }

    static std::shared_ptr<STValidation>
    makeValidation(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        return std::make_shared<STValidation>(
            NetClock::time_point{NetClock::duration{123456}},
            publicKey,
            secretKey,
            calcNodeID(publicKey),
            [](STValidation& v) {
                v.setFieldH256(sfLedgerHash, digest("ledger"));
                v.setFieldU32(sfLedgerSequence, 77);
                v.setFlag(vfFullValidation);
                v.setFieldU64(sfServerVersion, 1234);
                v.setFieldU64(sfCookie, 99);
                v.setFieldH256(sfValidatedHash, digest("validated"));
                v.setFieldV256(sfAmendments, STVector256(sfAmendments, {digest("a"), digest("b")}));
                v.setFieldU32(sfCloseTime, 456);
                v.setFieldU32(sfLoadFee, 789);
                v.setFieldU64(sfBaseFee, 10);
                v.setFieldU32(sfReserveBase, 200000);
                v.setFieldU32(sfReserveIncrement, 50000);
                v.setFieldAmount(sfBaseFeeDrops, XRPAmount{11});
                v.setFieldAmount(sfReserveBaseDrops, XRPAmount{210000});
                v.setFieldAmount(sfReserveIncrementDrops, XRPAmount{51000});
            });
    }

public:
    void
    run() override
    {
        testAllBadHeldTransactions();
        testModeAndLedgerFlags();
        testServerInfoAndValidationSubscriptions();
        testAuxiliaryStreamPublications();
        testPublicationSubscriptions();
        testSubmitTransactionGuards();
        testProcessTransactionPaths();
        testProcessTransactionSetMixed();
        testTransactionResultBranches();
    }

    void
    testAllBadHeldTransactions()
    {
        // All transactions are already marked as SF_BAD, and we should be able
        // to handle the case properly without an assertion failure
        testcase("No valid transactions in batch");

        std::string logs;

        {
            using namespace jtx;
            auto const alice = Account{"alice"};
            Env env{
                *this, envconfig(), std::make_unique<CaptureLogs>(&logs), beast::severities::kAll};
            env.memoize(env.master);
            env.memoize(alice);

            auto const jtx = env.jt(ticket::create(alice, 1), seq(1), fee(10));

            auto transactionId = jtx.stx->getTransactionID();
            env.app().getHashRouter().setFlags(transactionId, HashRouterFlags::HELD);

            env(jtx, json(jss::Sequence, 1), ter(terNO_ACCOUNT));

            env.app().getHashRouter().setFlags(transactionId, HashRouterFlags::BAD);

            env.close();
        }

        BEAST_EXPECT(logs.find("No transaction to process!") != std::string::npos);
    }

    void
    testModeAndLedgerFlags()
    {
        testcase("mode and ledger flags");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto& ops = env.app().getOPs();

        ops.setMode(OperatingMode::CONNECTED);
        BEAST_EXPECT(ops.getOperatingMode() == OperatingMode::CONNECTED);
        BEAST_EXPECT(ops.strOperatingMode(true) == "connected");
        BEAST_EXPECT(!ops.isFull());

        ops.setNeedNetworkLedger();
        BEAST_EXPECT(ops.isNeedNetworkLedger());
        BEAST_EXPECT(!ops.isFull());

        ops.clearNeedNetworkLedger();
        BEAST_EXPECT(!ops.isNeedNetworkLedger());

        ops.setStandAlone();
        BEAST_EXPECT(ops.getOperatingMode() == OperatingMode::FULL);
        BEAST_EXPECT(ops.isFull());

        auto const consensusInfo = ops.getConsensusInfo();
        BEAST_EXPECT(consensusInfo.isObject());

        auto const fetchInfo = ops.getLedgerFetchInfo();
        BEAST_EXPECT(fetchInfo.isObject());
        ops.clearLedgerFetch();

        Json::Value accounting;
        ops.stateAccounting(accounting);
        BEAST_EXPECT(accounting.isObject());

        ops.setStateTimer();
        ops.stop();
    }

    void
    testServerInfoAndValidationSubscriptions()
    {
        testcase("server info and validation subscriptions");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto& ops = env.app().getOPs();

        auto const adminInfo = ops.getServerInfo(true, true, false);
        auto const publicInfo = ops.getServerInfo(true, false, false);

        BEAST_EXPECT(adminInfo[jss::hostid].isString());
        BEAST_EXPECT(publicInfo[jss::hostid].isString());
        BEAST_EXPECT(adminInfo[jss::hostid].asString() == boost::asio::ip::host_name());
        BEAST_EXPECT(!publicInfo[jss::hostid].asString().empty());
        BEAST_EXPECT(publicInfo[jss::hostid].asString() != adminInfo[jss::hostid].asString());

        auto const sub = std::make_shared<TestInfoSub>(ops);
        BEAST_EXPECT(ops.subValidations(sub));
        BEAST_EXPECT(!ops.subValidations(sub));
        BEAST_EXPECT(ops.unsubValidations(sub->getSeq()));
        BEAST_EXPECT(!ops.unsubValidations(sub->getSeq()));
    }

    void
    testAuxiliaryStreamPublications()
    {
        testcase("auxiliary stream publications");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto& ops = env.app().getOPs();

        auto const serverSub = std::make_shared<TestInfoSub>(ops);
        auto const peerSub = std::make_shared<TestInfoSub>(ops);
        auto const manifestSub = std::make_shared<TestInfoSub>(ops);
        auto const validationV1Sub = std::make_shared<TestInfoSub>(ops);
        auto const validationV2Sub = std::make_shared<TestInfoSub>(ops);
        validationV2Sub->setApiVersion(2);

        Json::Value serverResult;
        BEAST_EXPECT(ops.subServer(serverSub, serverResult, false));
        BEAST_EXPECT(!ops.subServer(serverSub, serverResult, false));
        BEAST_EXPECT(serverResult[jss::server_status].isString());
        BEAST_EXPECT(serverResult[jss::random].isString());
        BEAST_EXPECT(serverResult[jss::hostid].isString());
        BEAST_EXPECT(serverResult[jss::pubkey_node].isString());
        BEAST_EXPECT(ops.subPeerStatus(peerSub));
        BEAST_EXPECT(ops.subManifests(manifestSub));
        BEAST_EXPECT(!ops.subManifests(manifestSub));
        BEAST_EXPECT(ops.subValidations(validationV1Sub));
        BEAST_EXPECT(ops.subValidations(validationV2Sub));

        auto const before = ops.getOperatingMode();
        ops.setMode(
            before == OperatingMode::TRACKING ? OperatingMode::FULL : OperatingMode::TRACKING);
        BEAST_EXPECT(serverSub->sentBroadcast);
        BEAST_EXPECT(serverSub->last[jss::type] == "serverStatus");
        BEAST_EXPECT(serverSub->last.isMember(jss::load_factor));
        BEAST_EXPECT(serverSub->last.isMember(jss::base_fee));

        ops.pubPeerStatus([]() {
            Json::Value jv(Json::objectValue);
            jv["state"] = "connected";
            return jv;
        });
        BEAST_EXPECT(peerSub->sentBroadcast);
        BEAST_EXPECT(peerSub->last[jss::type] == "peerStatusChange");
        BEAST_EXPECT(peerSub->last["state"] == "connected");

        auto const manifest = makeManifest();
        ops.pubManifest(manifest);
        BEAST_EXPECT(manifestSub->sentBroadcast);
        BEAST_EXPECT(manifestSub->last[jss::type] == "manifestReceived");
        BEAST_EXPECT(manifestSub->last[jss::master_key].isString());
        BEAST_EXPECT(manifestSub->last[jss::signing_key].isString());
        BEAST_EXPECT(manifestSub->last[jss::domain] == "example.com");
        BEAST_EXPECT(manifestSub->last.isMember(jss::signature));
        BEAST_EXPECT(manifestSub->last.isMember(jss::master_signature));
        BEAST_EXPECT(manifestSub->last.isMember(jss::manifest));

        auto const keys = randomKeyPair(KeyType::secp256k1);
        auto const validation = makeValidation(keys.first, keys.second);
        ops.pubValidation(validation);
        BEAST_EXPECT(validationV1Sub->sentBroadcast);
        BEAST_EXPECT(validationV2Sub->sentBroadcast);
        BEAST_EXPECT(validationV1Sub->last[jss::type] == "validationReceived");
        BEAST_EXPECT(validationV1Sub->last[jss::ledger_index].isString());
        BEAST_EXPECT(validationV1Sub->last.isMember(jss::cookie));
        BEAST_EXPECT(validationV1Sub->last.isMember(jss::validated_hash));
        BEAST_EXPECT(validationV1Sub->last.isMember(jss::amendments));
        BEAST_EXPECT(validationV1Sub->last.isMember(jss::base_fee));
        BEAST_EXPECT(validationV2Sub->last[jss::ledger_index].isUInt());
        BEAST_EXPECT(validationV2Sub->last[jss::network_id].isUInt());

        BEAST_EXPECT(ops.unsubServer(serverSub->getSeq()));
        BEAST_EXPECT(!ops.unsubServer(serverSub->getSeq()));
        BEAST_EXPECT(ops.unsubPeerStatus(peerSub->getSeq()));
        BEAST_EXPECT(ops.unsubManifests(manifestSub->getSeq()));
        BEAST_EXPECT(ops.unsubValidations(validationV1Sub->getSeq()));
        BEAST_EXPECT(ops.unsubValidations(validationV2Sub->getSeq()));
    }

    void
    testSubmitTransactionGuards()
    {
        testcase("submit transaction guards");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);

        auto makeTx = [&](Account const& from,
                          Account const& to,
                          STAmount const& amount,
                          std::uint32_t seqValue) -> std::shared_ptr<STTx const> {
            return env.jt(pay(from, to, amount), seq(seqValue)).stx;
        };

        auto& ops = env.app().getOPs();

        auto const waiting = makeTx(alice, bob, XRP(1), aliceSeq);
        ops.setNeedNetworkLedger();
        ops.submitTransaction(waiting);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(!env.current()->txExists(waiting->getTransactionID()));
        ops.clearNeedNetworkLedger();

        auto const cachedBad = makeTx(alice, bob, XRP(1), aliceSeq);
        env.app().getHashRouter().setFlags(cachedBad->getTransactionID(), HashRouterFlags::BAD);
        ops.submitTransaction(cachedBad);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(!env.current()->txExists(cachedBad->getTransactionID()));

        auto const good = makeTx(alice, bob, XRP(2), aliceSeq);
        ops.submitTransaction(good);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(env.current()->txExists(good->getTransactionID()));

        auto const blocked = makeTx(bob, alice, XRP(1), bobSeq);
        std::string reason;
        auto tx = std::make_shared<Transaction>(blocked, reason, env.app());
        env.app().getHashRouter().setFlags(tx->getID(), HashRouterFlags::BAD);
        ops.processTransaction(tx, false, true, NetworkOPs::FailHard::no);
        BEAST_EXPECT(tx->getStatus() == INVALID);
        BEAST_EXPECT(tx->getResult() == temBAD_SIGNATURE);
    }

    void
    testPublicationSubscriptions()
    {
        testcase("publication subscriptions");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env(pay(alice, bob, XRP(1)));
        env.close();

        auto& ops = env.app().getOPs();

        auto const rtSub = std::make_shared<TestInfoSub>(ops);
        auto const txSub = std::make_shared<TestInfoSub>(ops);
        auto const accountSub = std::make_shared<TestInfoSub>(ops);
        auto const rtAccountSub = std::make_shared<TestInfoSub>(ops);
        auto const ledgerSub = std::make_shared<TestInfoSub>(ops);
        auto const bookSub = std::make_shared<TestInfoSub>(ops);
        txSub->setApiVersion(2);

        hash_set<AccountID> const accounts{alice.id(), bob.id()};
        hash_set<AccountID> const rtAccounts{alice.id()};
        Json::Value ledgerResult;
        BEAST_EXPECT(ops.subRTTransactions(rtSub));
        BEAST_EXPECT(ops.subTransactions(txSub));
        BEAST_EXPECT(ops.subLedger(ledgerSub, ledgerResult));
        BEAST_EXPECT(ops.subBookChanges(bookSub));
        ops.subAccount(accountSub, accounts, false);
        ops.subAccount(rtAccountSub, rtAccounts, true);

        auto const proposed = env.jt(pay(alice, bob, XRP(2)), seq(env.seq(alice))).stx;
        ops.pubProposedTransaction(env.current(), proposed, tesSUCCESS);
        BEAST_EXPECT(rtSub->sentBroadcast);
        BEAST_EXPECT(rtSub->last.isObject());
        BEAST_EXPECT(rtSub->last[jss::status] == "proposed");
        BEAST_EXPECT(!rtSub->last[jss::validated].asBool());
        BEAST_EXPECT(rtAccountSub->last[jss::status] == "proposed");

        rtSub->last = Json::Value{};
        rtSub->sentBroadcast = false;
        rtAccountSub->last = Json::Value{};
        rtAccountSub->sentBroadcast = false;

        auto const inner = makeInnerBatchTx(env, alice, bob, XRP(2), env.seq(alice));
        BEAST_EXPECT(inner->isFlag(tfInnerBatchTxn));
        ops.pubProposedTransaction(env.current(), inner, tesSUCCESS);

        ops.setMode(OperatingMode::SYNCING);
        ops.pubLedger(env.closed());
        BEAST_EXPECT(txSub->sentBroadcast);
        BEAST_EXPECT(txSub->last[jss::type] == "transaction");
        BEAST_EXPECT(txSub->last[jss::validated].asBool());
        BEAST_EXPECT(txSub->last[jss::status] == "closed");
        BEAST_EXPECT(txSub->last.isMember(jss::tx_json));
        BEAST_EXPECT(txSub->last.isMember(jss::hash));
        BEAST_EXPECT(txSub->last.isMember(jss::meta));
        BEAST_EXPECT(accountSub->sentBroadcast);
        BEAST_EXPECT(accountSub->last[jss::validated].asBool());
        BEAST_EXPECT(rtAccountSub->sentBroadcast);
        BEAST_EXPECT(rtAccountSub->last[jss::validated].asBool());
        BEAST_EXPECT(ledgerSub->sentBroadcast);
        BEAST_EXPECT(ledgerSub->last[jss::type] == "ledgerClosed");
        BEAST_EXPECT(ledgerSub->last.isMember(jss::validated_ledgers));
        BEAST_EXPECT(bookSub->sentBroadcast);
        BEAST_EXPECT(bookSub->last.isObject());

        auto const ledgerSeq = env.closed()->seq();
        ops.pubLedger(env.closed());
        BEAST_EXPECT(ledgerSub->last[jss::ledger_index].asUInt() == ledgerSeq);

        ops.unsubAccount(accountSub, accounts, false);
        ops.unsubAccount(rtAccountSub, rtAccounts, true);
        BEAST_EXPECT(ops.unsubTransactions(txSub->getSeq()));
        BEAST_EXPECT(ops.unsubRTTransactions(rtSub->getSeq()));
        BEAST_EXPECT(ops.unsubLedger(ledgerSub->getSeq()));
        BEAST_EXPECT(ops.unsubBookChanges(bookSub->getSeq()));
    }

    void
    testProcessTransactionPaths()
    {
        testcase("process transaction paths");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto makeTx = [&](Account const& from, Account const& to, std::uint32_t seqValue) {
            auto jt = env.jt(pay(from, to, XRP(1)), seq(seqValue));
            std::string reason;
            return std::make_shared<Transaction>(jt.stx, reason, env.app());
        };

        auto local = makeTx(alice, bob, env.seq(alice));
        env.app().getOPs().processTransaction(local, false, true, NetworkOPs::FailHard::no);
        BEAST_EXPECT(env.current()->txExists(local->getID()));

        auto async = makeTx(bob, alice, env.seq(bob));
        env.app().getOPs().processTransaction(async, false, false, NetworkOPs::FailHard::no);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(env.current()->txExists(async->getID()));
    }

    void
    testProcessTransactionSetMixed()
    {
        testcase("mixed transaction set");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        CanonicalTXSet set(LedgerHash{});

        auto good = env.jt(pay(alice, bob, XRP(1)), seq(env.seq(alice))).stx;
        auto bad =
            env.jt(pay(alice, bob, XRP(1)), seq(env.seq(alice) + 1), txflags(tfInnerBatchTxn)).stx;

        set.insert(good);
        set.insert(bad);

        env.app().getOPs().processTransactionSet(set);

        BEAST_EXPECT(env.current()->txExists(good->getTransactionID()));
        BEAST_EXPECT(
            env.app().getHashRouter().getFlags(bad->getTransactionID()) == HashRouterFlags::BAD);
    }

    void
    testTransactionResultBranches()
    {
        testcase("transaction result branches");

        using namespace jtx;

        Env env{*this, envconfig()};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const dave = Account{"dave"};
        auto const erin = Account{"erin"};
        env.fund(XRP(10000), alice, bob);
        env.memoize(carol);
        env.memoize(dave);
        env.memoize(erin);
        env.close();

        auto makeTx = [&](JTx const& jt) {
            std::string reason;
            return std::make_shared<Transaction>(jt.stx, reason, env.app());
        };

        auto const aliceSeq = env.seq(alice);

        auto included = makeTx(env.jt(pay(alice, bob, XRP(1)), seq(aliceSeq)));
        env.app().getOPs().processTransaction(included, false, true, NetworkOPs::FailHard::no);
        BEAST_EXPECT(included->getStatus() == INCLUDED);
        BEAST_EXPECT(included->getSubmitResult().applied);
        BEAST_EXPECT(included->getSubmitResult().kept);

        auto obsolete = makeTx(env.jt(pay(alice, bob, XRP(1)), seq(aliceSeq)));
        env.app().getOPs().processTransaction(obsolete, false, true, NetworkOPs::FailHard::no);
        BEAST_EXPECT(obsolete->getStatus() == OBSOLETE);

        auto heldLocal = makeTx(env.jt(pay(carol, alice, XRP(1)), seq(1)));
        env.app().getOPs().processTransaction(heldLocal, false, true, NetworkOPs::FailHard::no);
        BEAST_EXPECT(heldLocal->getStatus() == HELD);
        BEAST_EXPECT(heldLocal->getSubmitResult().kept);

        auto failHardLocal = makeTx(env.jt(pay(dave, alice, XRP(1)), seq(1)));
        env.app().getOPs().processTransaction(
            failHardLocal, false, true, NetworkOPs::FailHard::yes);
        BEAST_EXPECT(!failHardLocal->getSubmitResult().kept);

        auto networkJtx = env.jt(pay(erin, alice, XRP(1)), seq(1));
        auto heldNetwork = makeTx(networkJtx);
        env.app().getOPs().processTransaction(heldNetwork, false, false, NetworkOPs::FailHard::no);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(heldNetwork->getStatus() == HELD);

        auto droppedNetwork = makeTx(networkJtx);
        env.app().getOPs().processTransaction(
            droppedNetwork, false, false, NetworkOPs::FailHard::no);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(!droppedNetwork->getSubmitResult().kept);
    }
};

BEAST_DEFINE_TESTSUITE(NetworkOPs, app, xrpl);

}  // namespace xrpl::test
