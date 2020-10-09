//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/LedgerReplayMsgHandler.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/basics/Slice.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <test/jtx.h>
#include <test/jtx/envconfig.h>

#include <chrono>
#include <thread>

namespace ripple {
namespace test {

struct LedgerReplay_test : public beast::unit_test::suite
{
    void
    run() override
    {
        testcase("Replay ledger");

        using namespace jtx;

        // Build a ledger normally
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env(*this);
        env.fund(XRP(100000), alice, bob);
        env.close();

        LedgerMaster& ledgerMaster = env.app().getLedgerMaster();
        auto const lastClosed = ledgerMaster.getClosedLedger();
        auto const lastClosedParent =
            ledgerMaster.getLedgerByHash(lastClosed->info().parentHash);

        auto const replayed = buildLedger(
            LedgerReplay(lastClosedParent, lastClosed),
            tapNONE,
            env.app(),
            env.journal);

        BEAST_EXPECT(replayed->info().hash == lastClosed->info().hash);
    }
};

enum class InboundLedgersBehavior {
    Good,
    DropAll,
};

/**
 * Simulate a network InboundLedgers.
 * Depending on the configured InboundLedgersBehavior,
 * it either provides the ledger or not
 */
class MagicInboundLedgers : public InboundLedgers
{
public:
    MagicInboundLedgers(
        LedgerMaster& ledgerSource,
        LedgerMaster& ledgerSink,
        InboundLedgersBehavior bhvr)
        : ledgerSource(ledgerSource), ledgerSink(ledgerSink), bhvr(bhvr)
    {
    }
    virtual ~MagicInboundLedgers() = default;

    virtual std::shared_ptr<Ledger const>
    acquire(uint256 const& hash, std::uint32_t seq, InboundLedger::Reason)
        override
    {
        if (bhvr == InboundLedgersBehavior::DropAll)
            return {};
        if (auto l = ledgerSource.getLedgerByHash(hash); l)
        {
            ledgerSink.storeLedger(l);
            return l;
        }

        return {};
    }

    virtual std::shared_ptr<InboundLedger>
    find(LedgerHash const& hash) override
    {
        return {};
    }

    virtual bool
    gotLedgerData(
        LedgerHash const& ledgerHash,
        std::shared_ptr<Peer>,
        std::shared_ptr<protocol::TMLedgerData>) override
    {
        return false;
    }

    virtual void
    gotStaleData(std::shared_ptr<protocol::TMLedgerData> packet) override
    {
    }

    virtual void
    logFailure(uint256 const& h, std::uint32_t seq) override
    {
    }

    virtual bool
    isFailure(uint256 const& h) override
    {
        return false;
    }

    virtual void
    clearFailures() override
    {
    }

    virtual Json::Value
    getInfo() override
    {
        return {};
    }

    virtual std::size_t
    fetchRate() override
    {
        return 0;
    }

    virtual void
    onLedgerFetched() override
    {
    }

    virtual void
    gotFetchPack() override
    {
    }
    virtual void
    sweep() override
    {
    }

    virtual void
    onStop() override
    {
    }

    LedgerMaster& ledgerSource;
    LedgerMaster& ledgerSink;
    InboundLedgersBehavior bhvr;
};

enum class PeerFeature {
    LedgerReplayEnabled,
    None,
};

/**
 * Simulate a network peer.
 * Depending on the configured PeerFeature,
 * it either supports the ProtocolFeature::LedgerReplay or not
 */
class TestPeer : public Peer
{
public:
    TestPeer(bool enableLedgerReplay) : ledgerReplayEnabled_(enableLedgerReplay)
    {
    }

    void
    send(std::shared_ptr<Message> const& m) override
    {
    }
    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return {};
    }
    void
    charge(Resource::Charge const& fee) override
    {
    }
    id_t
    id() const override
    {
        return 1234;
    }
    bool
    cluster() const override
    {
        return false;
    }
    bool
    isHighLatency() const override
    {
        return false;
    }
    int
    getScore(bool) const override
    {
        return 0;
    }
    PublicKey const&
    getNodePublic() const override
    {
        static PublicKey key{};
        return key;
    }
    Json::Value
    json() override
    {
        return {};
    }
    bool
    supportsFeature(ProtocolFeature f) const override
    {
        if (f == ProtocolFeature::LedgerReplay && ledgerReplayEnabled_)
            return true;
        return false;
    }
    std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }
    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }
    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 hash{};
        return hash;
    }
    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override
    {
        return true;
    }
    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override
    {
    }
    bool
    hasTxSet(uint256 const& hash) const override
    {
        return false;
    }
    void
    cycleStatus() override
    {
    }
    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override
    {
        return false;
    }
    bool
    compressionEnabled() const override
    {
        return false;
    }

    bool ledgerReplayEnabled_;
};

enum class PeerSetBehavior {
    Good,
    Drop50,
    DropAll,
    DropSkipListReply,
    DropLedgerDeltaReply,
    Repeat,
};

/**
 * Simulate a peerSet that supplies peers to ledger replay subtasks.
 * It connects the ledger replay client side and server side message handlers.
 * Depending on the configured PeerSetBehavior,
 * it may drop or repeat some of the messages.
 */
struct TestPeerSet : public PeerSet
{
    TestPeerSet(
        LedgerReplayMsgHandler& me,
        LedgerReplayMsgHandler& other,
        PeerSetBehavior bhvr,
        bool enableLedgerReplay)
        : local(me)
        , remote(other)
        , dummyPeer(std::make_shared<TestPeer>(enableLedgerReplay))
        , behavior(bhvr)
    {
    }

    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override
    {
        hasItem(dummyPeer);
        onPeerAdded(dummyPeer);
    }

    void
    sendRequest(
        ::google::protobuf::Message const& msg,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override
    {
        int dropRate = 0;
        if (behavior == PeerSetBehavior::Drop50)
            dropRate = 50;
        else if (behavior == PeerSetBehavior::DropAll)
            dropRate = 100;

        if ((rand() % 100 + 1) <= dropRate)
            return;

        switch (type)
        {
            case protocol::mtPROOF_PATH_REQ: {
                if (behavior == PeerSetBehavior::DropSkipListReply)
                    return;
                auto request = std::make_shared<protocol::TMProofPathRequest>(
                    dynamic_cast<protocol::TMProofPathRequest const&>(msg));
                auto reply = std::make_shared<protocol::TMProofPathResponse>(
                    remote.processProofPathRequest(request));
                local.processProofPathResponse(reply);
                if (behavior == PeerSetBehavior::Repeat)
                    local.processProofPathResponse(reply);
                break;
            }
            case protocol::mtREPLAY_DELTA_REQ: {
                if (behavior == PeerSetBehavior::DropLedgerDeltaReply)
                    return;
                auto request = std::make_shared<protocol::TMReplayDeltaRequest>(
                    dynamic_cast<protocol::TMReplayDeltaRequest const&>(msg));
                auto reply = std::make_shared<protocol::TMReplayDeltaResponse>(
                    remote.processReplayDeltaRequest(request));
                local.processReplayDeltaResponse(reply);
                if (behavior == PeerSetBehavior::Repeat)
                    local.processReplayDeltaResponse(reply);
                break;
            }
            default:
                return;
        }
    }

    const std::set<Peer::id_t>&
    getPeerIds() const override
    {
        static std::set<Peer::id_t> emptyPeers;
        return emptyPeers;
    }

    LedgerReplayMsgHandler& local;
    LedgerReplayMsgHandler& remote;
    std::shared_ptr<TestPeer> dummyPeer;
    PeerSetBehavior behavior;
};

/**
 * Build the TestPeerSet.
 */
class TestPeerSetBuilder : public PeerSetBuilder
{
public:
    TestPeerSetBuilder(
        LedgerReplayMsgHandler& me,
        LedgerReplayMsgHandler& other,
        PeerSetBehavior bhvr,
        PeerFeature peerFeature)
        : local(me)
        , remote(other)
        , behavior(bhvr)
        , enableLedgerReplay(peerFeature == PeerFeature::LedgerReplayEnabled)
    {
    }

    std::unique_ptr<PeerSet>
    build() override
    {
        return std::make_unique<TestPeerSet>(
            local, remote, behavior, enableLedgerReplay);
    }

private:
    LedgerReplayMsgHandler& local;
    LedgerReplayMsgHandler& remote;
    PeerSetBehavior behavior;
    bool enableLedgerReplay;
};

/**
 * Utility class for (1) creating ledgers with txns and
 * (2) providing the ledgers via the ledgerMaster
 */
struct LedgerServer
{
    struct Parameter
    {
        int initLedgers;
        int initAccounts = 10;
        int initAmount = 1'000'000;
        int numTxPerLedger = 10;
        int txAmount = 10;
    };

    LedgerServer(beast::unit_test::suite& suite, Parameter const& p)
        : env(suite)
        , app(env.app())
        , ledgerMaster(env.app().getLedgerMaster())
        , msgHandler(env.app(), env.app().getLedgerReplayer())
        , param(p)
    {
        assert(param.initLedgers > 0);
        createAccounts(param.initAccounts);
        createLedgerHistory();
        app.logs().threshold(beast::severities::Severity::kWarning);
    }

    /**
     * @note close a ledger
     */
    void
    createAccounts(int newAccounts)
    {
        auto fundedAccounts = accounts.size();
        for (int i = 0; i < newAccounts; ++i)
        {
            accounts.emplace_back(
                "alice_" + std::to_string(fundedAccounts + i));
            env.fund(jtx::XRP(param.initAmount), accounts.back());
        }
        env.close();
    }

    /**
     * @note close a ledger
     */
    void
    sendPayments(int newTxes)
    {
        int fundedAccounts = accounts.size();
        assert(fundedAccounts >= newTxes);
        std::unordered_set<int> senders;

        // somewhat random but reproducible
        int r = ledgerMaster.getClosedLedger()->seq() * 7;
        int fromIdx = 0;
        int toIdx = 0;
        auto updateIdx = [&]() {
            assert(fundedAccounts > senders.size());
            fromIdx = (fromIdx + r) % fundedAccounts;
            while (senders.count(fromIdx) != 0)
                fromIdx = (fromIdx + 1) % fundedAccounts;
            senders.insert(fromIdx);
            toIdx = (toIdx + r * 2) % fundedAccounts;
            if (toIdx == fromIdx)
                toIdx = (toIdx + 1) % fundedAccounts;
        };

        for (int i = 0; i < newTxes; ++i)
        {
            updateIdx();
            env.apply(
                pay(accounts[fromIdx],
                    accounts[toIdx],
                    jtx::drops(ledgerMaster.getClosedLedger()->fees().base) +
                        jtx::XRP(param.txAmount)),
                jtx::seq(jtx::autofill),
                jtx::fee(jtx::autofill),
                jtx::sig(jtx::autofill));
        }
        env.close();
    }

    /**
     * create ledger history
     */
    void
    createLedgerHistory()
    {
        for (int i = 0; i < param.initLedgers - 1; ++i)
        {
            sendPayments(param.numTxPerLedger);
        }
    }

    jtx::Env env;
    Application& app;
    LedgerMaster& ledgerMaster;
    LedgerReplayMsgHandler msgHandler;
    Parameter param;
    std::vector<jtx::Account> accounts;
};

enum class TaskStatus {
    Failed,
    Completed,
    NotDone,
    NotExist,
};

/**
 * Ledger replay client side.
 * It creates the LedgerReplayer which has the client side logic.
 * The client side and server side message handlers are connect via
 * the peerSet to pass the requests and responses.
 * It also has utility functions for checking task status
 */
class LedgerReplayClient
{
public:
    LedgerReplayClient(
        beast::unit_test::suite& suite,
        LedgerServer& server,
        PeerSetBehavior behavior = PeerSetBehavior::Good,
        InboundLedgersBehavior inboundBhvr = InboundLedgersBehavior::Good,
        PeerFeature peerFeature = PeerFeature::LedgerReplayEnabled)
        : env(suite, jtx::envconfig(jtx::port_increment, 3))
        , app(env.app())
        , ledgerMaster(env.app().getLedgerMaster())
        , inboundLedgers(
              server.app.getLedgerMaster(),
              ledgerMaster,
              inboundBhvr)
        , serverMsgHandler(server.app, server.app.getLedgerReplayer())
        , clientMsgHandler(env.app(), replayer)
        , stopableParent("replayerStopParent")
        , replayer(
              env.app(),
              inboundLedgers,
              std::make_unique<TestPeerSetBuilder>(
                  clientMsgHandler,
                  serverMsgHandler,
                  behavior,
                  peerFeature),
              stopableParent)
    {
    }

    void
    addLedger(std::shared_ptr<Ledger const> const& l)
    {
        ledgerMaster.storeLedger(l);
    }

    bool
    haveLedgers(uint256 const& finishLedgerHash, int totalReplay)
    {
        uint256 hash = finishLedgerHash;
        int i = 0;
        for (; i < totalReplay; ++i)
        {
            auto const l = ledgerMaster.getLedgerByHash(hash);
            if (!l)
                return false;
            hash = l->info().parentHash;
        }
        return true;
    }

    bool
    waitForLedgers(uint256 const& finishLedgerHash, int totalReplay)
    {
        int totalRound = 100;
        for (int i = 0; i < totalRound; ++i)
        {
            if (haveLedgers(finishLedgerHash, totalReplay))
                return true;
            if (i < totalRound - 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    bool
    waitForDone()
    {
        int totalRound = 100;
        for (int i = 0; i < totalRound; ++i)
        {
            bool allDone = true;
            {
                std::unique_lock<std::mutex> lock(replayer.mtx_);
                for (auto const& t : replayer.tasks_)
                {
                    if (!t->finished())
                    {
                        allDone = false;
                        break;
                    }
                }
            }
            if (allDone)
                return true;
            if (i < totalRound - 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    std::vector<std::shared_ptr<LedgerReplayTask>>
    getTasks()
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        return replayer.tasks_;
    }

    std::shared_ptr<LedgerReplayTask>
    findTask(uint256 const& hash, int totalReplay)
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        auto i = std::find_if(
            replayer.tasks_.begin(), replayer.tasks_.end(), [&](auto const& t) {
                return t->parameter_.finishHash_ == hash &&
                    t->parameter_.totalLedgers_ == totalReplay;
            });
        if (i == replayer.tasks_.end())
            return {};
        return *i;
    }

    std::size_t
    countDeltas()
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        return replayer.deltas_.size();
    }

    std::size_t
    countSkipLists()
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        return replayer.skipLists_.size();
    }

    bool
    countsAsExpected(
        std::size_t tasks,
        std::size_t skipLists,
        std::size_t deltas)
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        return replayer.tasks_.size() == tasks &&
            replayer.skipLists_.size() == skipLists &&
            replayer.deltas_.size() == deltas;
    }

    std::shared_ptr<SkipListAcquire>
    findSkipListAcquire(uint256 const& hash)
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        auto i = replayer.skipLists_.find(hash);
        if (i == replayer.skipLists_.end())
            return {};
        return i->second.lock();
    }

    std::shared_ptr<LedgerDeltaAcquire>
    findLedgerDeltaAcquire(uint256 const& hash)
    {
        std::unique_lock<std::mutex> lock(replayer.mtx_);
        auto i = replayer.deltas_.find(hash);
        if (i == replayer.deltas_.end())
            return {};
        return i->second.lock();
    }

    template <typename T>
    TaskStatus
    taskStatus(std::shared_ptr<T> const& t)
    {
        if (t->failed_)
            return TaskStatus::Failed;
        if (t->complete_)
            return TaskStatus::Completed;
        return TaskStatus::NotDone;
    }

    bool
    asExpected(
        std::shared_ptr<LedgerReplayTask> const& task,
        TaskStatus taskExpect,
        TaskStatus skiplistExpect,
        std::vector<TaskStatus> const& deltaExpects)
    {
        if (taskStatus(task) == taskExpect)
        {
            if (taskStatus(task->skipListAcquirer_) == skiplistExpect)
            {
                if (task->deltas_.size() == deltaExpects.size())
                {
                    for (int i = 0; i < deltaExpects.size(); ++i)
                    {
                        if (taskStatus(task->deltas_[i]) != deltaExpects[i])
                            return false;
                    }
                    return true;
                }
            }
        }
        return false;
    }

    bool
    asExpected(
        uint256 const& hash,
        int totalReplay,
        TaskStatus taskExpect,
        TaskStatus skiplistExpect,
        std::vector<TaskStatus> const& deltaExpects)
    {
        auto t = findTask(hash, totalReplay);
        if (!t)
        {
            if (taskExpect == TaskStatus::NotExist)
                return true;
            return false;
        }

        return asExpected(t, taskExpect, skiplistExpect, deltaExpects);
    }

    bool
    checkStatus(
        uint256 const& hash,
        int totalReplay,
        TaskStatus taskExpect,
        TaskStatus skiplistExpect,
        std::vector<TaskStatus> const& deltaExpects)
    {
        auto t = findTask(hash, totalReplay);
        if (!t)
        {
            if (taskExpect == TaskStatus::NotExist)
                return true;
            return false;
        }

        return asExpected(t, taskExpect, skiplistExpect, deltaExpects);
    }

    bool
    waitAndCheckStatus(
        uint256 const& hash,
        int totalReplay,
        TaskStatus taskExpect,
        TaskStatus skiplistExpect,
        std::vector<TaskStatus> const& deltaExpects)
    {
        if (!waitForDone())
            return false;

        return checkStatus(
            hash, totalReplay, taskExpect, skiplistExpect, deltaExpects);
    }

    jtx::Env env;
    Application& app;
    LedgerMaster& ledgerMaster;
    MagicInboundLedgers inboundLedgers;
    LedgerReplayMsgHandler serverMsgHandler;
    LedgerReplayMsgHandler clientMsgHandler;
    RootStoppable stopableParent;
    LedgerReplayer replayer;
};

using namespace beast::severities;
void
logAll(
    LedgerServer& server,
    LedgerReplayClient& client,
    beast::severities::Severity level = Severity::kTrace)
{
    server.app.logs().threshold(level);
    client.app.logs().threshold(level);
}
// logAll(net.server, net.client);

/*
 * Create a LedgerServer and a LedgerReplayClient
 */
struct NetworkOfTwo
{
    NetworkOfTwo(
        beast::unit_test::suite& suite,
        LedgerServer::Parameter const& param,
        PeerSetBehavior behavior = PeerSetBehavior::Good,
        InboundLedgersBehavior inboundBhvr = InboundLedgersBehavior::Good,
        PeerFeature peerFeature = PeerFeature::LedgerReplayEnabled)
        : server(suite, param)
        , client(suite, server, behavior, inboundBhvr, peerFeature)
    {
        // logAll(server, client);
    }
    LedgerServer server;
    LedgerReplayClient client;
};

/**
 * Test cases:
 * LedgerReplayer_test:
 * -- process TMProofPathRequest and TMProofPathResponse
 * -- process TMReplayDeltaRequest and TMReplayDeltaResponse
 * -- update and merge LedgerReplayTask::TaskParameter
 * -- process [ledger_replay] section in config
 * -- peer handshake
 * -- replay a range of ledgers that the local node already has
 * -- replay a range of ledgers and fallback to InboundLedgers because
 *    peers do not support ProtocolFeature::LedgerReplay
 * -- replay a range of ledgers and the network drops or repeats messages
 * -- call onStop() and the tasks and subtasks are removed
 * -- process a bad skip list
 * -- process a bad ledger delta
 * -- replay ledger ranges with different overlaps
 *
 * LedgerReplayerTimeout_test:
 * -- timeouts of SkipListAcquire
 * -- timeouts of LedgerDeltaAcquire
 *
 * LedgerReplayerLong_test: (MANUAL)
 * -- call replayer.replay() 4 times to replay 1000 ledgers
 */

struct LedgerReplayer_test : public beast::unit_test::suite
{
    void
    testProofPath()
    {
        testcase("ProofPath");
        LedgerServer server(*this, {1});
        auto const l = server.ledgerMaster.getClosedLedger();

        {
            // request, missing key
            auto request = std::make_shared<protocol::TMProofPathRequest>();
            request->set_ledgerhash(
                l->info().hash.data(), l->info().hash.size());
            request->set_type(protocol::TMLedgerMapType::lmACCOUNT_STATE);
            auto reply = std::make_shared<protocol::TMProofPathResponse>(
                server.msgHandler.processProofPathRequest(request));
            BEAST_EXPECT(reply->has_error());
            BEAST_EXPECT(!server.msgHandler.processProofPathResponse(reply));
        }
        {
            // request, wrong hash
            auto request = std::make_shared<protocol::TMProofPathRequest>();
            request->set_type(protocol::TMLedgerMapType::lmACCOUNT_STATE);
            request->set_key(
                keylet::skip().key.data(), keylet::skip().key.size());
            uint256 hash(1234567);
            request->set_ledgerhash(hash.data(), hash.size());
            auto reply = std::make_shared<protocol::TMProofPathResponse>(
                server.msgHandler.processProofPathRequest(request));
            BEAST_EXPECT(reply->has_error());
        }

        {
            // good request
            auto request = std::make_shared<protocol::TMProofPathRequest>();
            request->set_ledgerhash(
                l->info().hash.data(), l->info().hash.size());
            request->set_type(protocol::TMLedgerMapType::lmACCOUNT_STATE);
            request->set_key(
                keylet::skip().key.data(), keylet::skip().key.size());
            // generate response
            auto reply = std::make_shared<protocol::TMProofPathResponse>(
                server.msgHandler.processProofPathRequest(request));
            BEAST_EXPECT(!reply->has_error());
            BEAST_EXPECT(server.msgHandler.processProofPathResponse(reply));

            {
                // bad reply
                // bad header
                std::string r(reply->ledgerheader());
                r.back()--;
                reply->set_ledgerheader(r);
                BEAST_EXPECT(
                    !server.msgHandler.processProofPathResponse(reply));
                r.back()++;
                reply->set_ledgerheader(r);
                BEAST_EXPECT(server.msgHandler.processProofPathResponse(reply));
                // bad proof path
                reply->mutable_path()->RemoveLast();
                BEAST_EXPECT(
                    !server.msgHandler.processProofPathResponse(reply));
            }
        }
    }

    void
    testReplayDelta()
    {
        testcase("ReplayDelta");
        LedgerServer server(*this, {1});
        auto const l = server.ledgerMaster.getClosedLedger();

        {
            // request, missing hash
            auto request = std::make_shared<protocol::TMReplayDeltaRequest>();
            auto reply = std::make_shared<protocol::TMReplayDeltaResponse>(
                server.msgHandler.processReplayDeltaRequest(request));
            BEAST_EXPECT(reply->has_error());
            BEAST_EXPECT(!server.msgHandler.processReplayDeltaResponse(reply));
            // request, wrong hash
            uint256 hash(1234567);
            request->set_ledgerhash(hash.data(), hash.size());
            reply = std::make_shared<protocol::TMReplayDeltaResponse>(
                server.msgHandler.processReplayDeltaRequest(request));
            BEAST_EXPECT(reply->has_error());
            BEAST_EXPECT(!server.msgHandler.processReplayDeltaResponse(reply));
        }

        {
            // good request
            auto request = std::make_shared<protocol::TMReplayDeltaRequest>();
            request->set_ledgerhash(
                l->info().hash.data(), l->info().hash.size());
            auto reply = std::make_shared<protocol::TMReplayDeltaResponse>(
                server.msgHandler.processReplayDeltaRequest(request));
            BEAST_EXPECT(!reply->has_error());
            BEAST_EXPECT(server.msgHandler.processReplayDeltaResponse(reply));

            {
                // bad reply
                // bad header
                std::string r(reply->ledgerheader());
                r.back()--;
                reply->set_ledgerheader(r);
                BEAST_EXPECT(
                    !server.msgHandler.processReplayDeltaResponse(reply));
                r.back()++;
                reply->set_ledgerheader(r);
                BEAST_EXPECT(
                    server.msgHandler.processReplayDeltaResponse(reply));
                // bad txns
                reply->mutable_transaction()->RemoveLast();
                BEAST_EXPECT(
                    !server.msgHandler.processReplayDeltaResponse(reply));
            }
        }
    }

    void
    testTaskParameter()
    {
        testcase("TaskParameter");

        auto makeSkipList = [](int count) -> std::vector<uint256> const {
            std::vector<uint256> sList;
            for (int i = 0; i < count; ++i)
                sList.emplace_back(i);
            return sList;
        };

        LedgerReplayTask::TaskParameter tp10(
            InboundLedger::Reason::GENERIC, uint256(10), 10);
        BEAST_EXPECT(!tp10.update(uint256(777), 5, makeSkipList(10)));
        BEAST_EXPECT(!tp10.update(uint256(10), 5, makeSkipList(8)));
        BEAST_EXPECT(tp10.update(uint256(10), 10, makeSkipList(10)));

        // can merge to self
        BEAST_EXPECT(tp10.canMergeInto(tp10));

        // smaller task
        LedgerReplayTask::TaskParameter tp9(
            InboundLedger::Reason::GENERIC, uint256(9), 9);

        BEAST_EXPECT(tp9.canMergeInto(tp10));
        BEAST_EXPECT(!tp10.canMergeInto(tp9));

        tp9.totalLedgers_++;
        BEAST_EXPECT(!tp9.canMergeInto(tp10));
        tp9.totalLedgers_--;
        BEAST_EXPECT(tp9.canMergeInto(tp10));

        tp9.reason_ = InboundLedger::Reason::CONSENSUS;
        BEAST_EXPECT(!tp9.canMergeInto(tp10));
        tp9.reason_ = InboundLedger::Reason::GENERIC;
        BEAST_EXPECT(tp9.canMergeInto(tp10));

        tp9.finishHash_ = uint256(1234);
        BEAST_EXPECT(!tp9.canMergeInto(tp10));
        tp9.finishHash_ = uint256(9);
        BEAST_EXPECT(tp9.canMergeInto(tp10));

        // larger task
        LedgerReplayTask::TaskParameter tp20(
            InboundLedger::Reason::GENERIC, uint256(20), 20);
        BEAST_EXPECT(tp20.update(uint256(20), 20, makeSkipList(20)));
        BEAST_EXPECT(tp10.canMergeInto(tp20));
        BEAST_EXPECT(tp9.canMergeInto(tp20));
        BEAST_EXPECT(!tp20.canMergeInto(tp10));
        BEAST_EXPECT(!tp20.canMergeInto(tp9));
    }

    void
    testConfig()
    {
        testcase("config test");
        {
            Config c;
            BEAST_EXPECT(c.LEDGER_REPLAY == false);
        }

        {
            Config c;
            std::string toLoad(R"rippleConfig(
[ledger_replay]
1
)rippleConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.LEDGER_REPLAY == true);
        }

        {
            Config c;
            std::string toLoad = (R"rippleConfig(
[ledger_replay]
0
)rippleConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.LEDGER_REPLAY == false);
        }
    }

    void
    testHandshake()
    {
        testcase("handshake test");
        auto handshake = [&](bool client, bool server, bool expecting) -> bool {
            auto request = ripple::makeRequest(true, false, false, client);
            http_request_type http_request;
            http_request.version(request.version());
            http_request.base() = request.base();
            bool serverResult =
                peerFeatureEnabled(http_request, FEATURE_LEDGER_REPLAY, server);
            if (serverResult != expecting)
                return false;

            beast::IP::Address addr =
                boost::asio::ip::address::from_string("172.1.1.100");
            jtx::Env serverEnv(*this);
            serverEnv.app().config().LEDGER_REPLAY = server;
            auto http_resp = ripple::makeResponse(
                true,
                http_request,
                addr,
                addr,
                uint256{1},
                1,
                {1, 0},
                serverEnv.app());
            auto const clientResult =
                peerFeatureEnabled(http_resp, FEATURE_LEDGER_REPLAY, client);
            if (clientResult != expecting)
                return false;

            return true;
        };

        BEAST_EXPECT(handshake(false, false, false));
        BEAST_EXPECT(handshake(false, true, false));
        BEAST_EXPECT(handshake(true, false, false));
        BEAST_EXPECT(handshake(true, true, true));
    }

    void
    testAllLocal(int totalReplay)
    {
        testcase("local node has all the ledgers");
        auto psBhvr = PeerSetBehavior::DropAll;
        auto ilBhvr = InboundLedgersBehavior::DropAll;
        auto peerFeature = PeerFeature::None;

        NetworkOfTwo net(*this, {totalReplay + 1}, psBhvr, ilBhvr, peerFeature);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        for (int i = 0; i < totalReplay; ++i)
        {
            BEAST_EXPECT(l);
            if (l)
            {
                net.client.ledgerMaster.storeLedger(l);
                l = net.server.ledgerMaster.getLedgerByHash(
                    l->info().parentHash);
            }
            else
                break;
        }

        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Completed);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));

        // sweep
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    testAllInboundLedgers(int totalReplay)
    {
        testcase("all the ledgers from InboundLedgers");
        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            PeerSetBehavior::DropAll,
            InboundLedgersBehavior::Good,
            PeerFeature::None);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Completed);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));

        // sweep
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    testPeerSetBehavior(PeerSetBehavior peerSetBehavior, int totalReplay = 4)
    {
        switch (peerSetBehavior)
        {
            case PeerSetBehavior::Good:
                testcase("good network");
                break;
            case PeerSetBehavior::Drop50:
                testcase("network drops 50% messages");
                break;
            case PeerSetBehavior::Repeat:
                testcase("network repeats all messages");
                break;
            default:
                return;
        }

        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            peerSetBehavior,
            InboundLedgersBehavior::DropAll,
            PeerFeature::LedgerReplayEnabled);

        // feed client with start ledger since InboundLedgers drops all
        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        for (int i = 0; i < totalReplay - 1; ++i)
        {
            l = net.server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        }
        net.client.ledgerMaster.storeLedger(l);

        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Completed);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));
        BEAST_EXPECT(net.client.waitForLedgers(finalHash, totalReplay));

        // sweep
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    testOnStop()
    {
        testcase("onStop before timeout");
        int totalReplay = 3;
        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            PeerSetBehavior::DropAll,
            InboundLedgersBehavior::Good,
            PeerFeature::LedgerReplayEnabled);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses;
        BEAST_EXPECT(net.client.checkStatus(
            finalHash,
            totalReplay,
            TaskStatus::NotDone,
            TaskStatus::NotDone,
            deltaStatuses));

        // onStop
        BEAST_EXPECT(net.client.countsAsExpected(1, 1, 0));
        net.client.replayer.onStop();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    testSkipListBadReply()
    {
        testcase("SkipListAcquire bad reply");
        int totalReplay = 3;
        NetworkOfTwo net(
            *this,
            {totalReplay + 1 + 1},
            PeerSetBehavior::DropAll,
            InboundLedgersBehavior::DropAll,
            PeerFeature::LedgerReplayEnabled);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        auto skipList = net.client.findSkipListAcquire(finalHash);

        std::uint8_t payload[55] = {
            0x6A, 0x09, 0xE6, 0x67, 0xF3, 0xBC, 0xC9, 0x08, 0xB2};
        auto item = std::make_shared<SHAMapItem>(
            uint256(12345), Slice(payload, sizeof(payload)));
        skipList->processData(l->seq(), item);

        std::vector<TaskStatus> deltaStatuses;
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Failed,
            TaskStatus::Failed,
            deltaStatuses));

        // add another task
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay + 1);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Failed,
            TaskStatus::Failed,
            deltaStatuses));
        BEAST_EXPECT(net.client.countsAsExpected(2, 1, 0));
    }

    void
    testLedgerDeltaBadReply()
    {
        testcase("LedgerDeltaAcquire bad reply");
        int totalReplay = 3;
        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            PeerSetBehavior::DropLedgerDeltaReply,
            InboundLedgersBehavior::DropAll,
            PeerFeature::LedgerReplayEnabled);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.ledgerMaster.storeLedger(l);
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        auto delta = net.client.findLedgerDeltaAcquire(l->info().parentHash);
        delta->processData(
            l->info(),  // wrong ledger info
            std::map<std::uint32_t, std::shared_ptr<STTx const>>());
        BEAST_EXPECT(net.client.taskStatus(delta) == TaskStatus::Failed);
        BEAST_EXPECT(
            net.client.taskStatus(net.client.findTask(
                finalHash, totalReplay)) == TaskStatus::Failed);

        // add another task
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay + 1);
        BEAST_EXPECT(
            net.client.taskStatus(net.client.findTask(
                finalHash, totalReplay + 1)) == TaskStatus::Failed);
    }

    void
    testLedgerReplayOverlap()
    {
        testcase("Overlap tasks");
        int totalReplay = 5;
        NetworkOfTwo net(
            *this,
            {totalReplay * 3 + 1},
            PeerSetBehavior::Good,
            InboundLedgersBehavior::Good,
            PeerFeature::LedgerReplayEnabled);
        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);
        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Completed);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));
        BEAST_EXPECT(net.client.waitForLedgers(finalHash, totalReplay));

        // same range, same reason
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);
        BEAST_EXPECT(net.client.countsAsExpected(1, 1, totalReplay - 1));
        // same range, different reason
        net.client.replayer.replay(
            InboundLedger::Reason::CONSENSUS, finalHash, totalReplay);
        BEAST_EXPECT(net.client.countsAsExpected(2, 1, totalReplay - 1));

        // no overlap
        for (int i = 0; i < totalReplay + 2; ++i)
        {
            l = net.server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        }
        auto finalHash_early = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash_early, totalReplay);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash_early,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));  // deltaStatuses no change
        BEAST_EXPECT(net.client.waitForLedgers(finalHash_early, totalReplay));
        BEAST_EXPECT(net.client.countsAsExpected(3, 2, 2 * (totalReplay - 1)));

        // partial overlap
        l = net.server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        auto finalHash_moreEarly = l->info().parentHash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash_moreEarly, totalReplay);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash_moreEarly,
            totalReplay,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));  // deltaStatuses no change
        BEAST_EXPECT(
            net.client.waitForLedgers(finalHash_moreEarly, totalReplay));
        BEAST_EXPECT(
            net.client.countsAsExpected(4, 3, 2 * (totalReplay - 1) + 2));

        // cover
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay * 3);
        deltaStatuses =
            std::vector<TaskStatus>(totalReplay * 3 - 1, TaskStatus::Completed);
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay * 3,
            TaskStatus::Completed,
            TaskStatus::Completed,
            deltaStatuses));  // deltaStatuses changed
        BEAST_EXPECT(net.client.waitForLedgers(finalHash, totalReplay * 3));
        BEAST_EXPECT(net.client.countsAsExpected(5, 3, totalReplay * 3 - 1));

        // sweep
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    run() override
    {
        testProofPath();
        testReplayDelta();
        testTaskParameter();
        testConfig();
        testHandshake();
        testAllLocal(1);
        testAllLocal(3);
        testAllInboundLedgers(1);
        testAllInboundLedgers(4);
        testPeerSetBehavior(PeerSetBehavior::Good, 1);
        testPeerSetBehavior(PeerSetBehavior::Good);
        testPeerSetBehavior(PeerSetBehavior::Drop50);
        testPeerSetBehavior(PeerSetBehavior::Repeat);
        testOnStop();
        testSkipListBadReply();
        testLedgerDeltaBadReply();
        testLedgerReplayOverlap();
    }
};

struct LedgerReplayerTimeout_test : public beast::unit_test::suite
{
    void
    testSkipListTimeout()
    {
        testcase("SkipListAcquire timeout");
        int totalReplay = 3;
        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            PeerSetBehavior::DropAll,
            InboundLedgersBehavior::Good,
            PeerFeature::LedgerReplayEnabled);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses;
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Failed,
            TaskStatus::Failed,
            deltaStatuses));

        // sweep
        BEAST_EXPECT(net.client.countsAsExpected(1, 1, 0));
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    testLedgerDeltaTimeout()
    {
        testcase("LedgerDeltaAcquire timeout");
        int totalReplay = 3;
        NetworkOfTwo net(
            *this,
            {totalReplay + 1},
            PeerSetBehavior::DropAll,
            InboundLedgersBehavior::Good,
            PeerFeature::LedgerReplayEnabled);

        auto l = net.server.ledgerMaster.getClosedLedger();
        uint256 finalHash = l->info().hash;
        net.client.ledgerMaster.storeLedger(l);
        net.client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Failed);
        deltaStatuses.back() = TaskStatus::Completed;  // in client ledgerMaster
        BEAST_EXPECT(net.client.waitAndCheckStatus(
            finalHash,
            totalReplay,
            TaskStatus::Failed,
            TaskStatus::Completed,
            deltaStatuses));

        // sweep
        BEAST_EXPECT(net.client.countsAsExpected(1, 1, totalReplay - 1));
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }

    void
    run() override
    {
        testSkipListTimeout();
        testLedgerDeltaTimeout();
    }
};

struct LedgerReplayerLong_test : public beast::unit_test::suite
{
    void
    run() override
    {
        testcase("Acquire 1000 ledgers");
        int totalReplay = 250;
        int rounds = 4;
        NetworkOfTwo net(
            *this,
            {totalReplay * rounds + 1},
            PeerSetBehavior::Good,
            InboundLedgersBehavior::Good,
            PeerFeature::LedgerReplayEnabled);

        std::vector<uint256> finishHashes;
        auto l = net.server.ledgerMaster.getClosedLedger();
        for (int i = 0; i < rounds; ++i)
        {
            finishHashes.push_back(l->info().hash);
            for (int j = 0; j < totalReplay; ++j)
            {
                l = net.server.ledgerMaster.getLedgerByHash(
                    l->info().parentHash);
            }
        }
        BEAST_EXPECT(finishHashes.size() == rounds);

        for (int i = 0; i < rounds; ++i)
        {
            net.client.replayer.replay(
                InboundLedger::Reason::GENERIC, finishHashes[i], totalReplay);
        }

        std::vector<TaskStatus> deltaStatuses(
            totalReplay - 1, TaskStatus::Completed);
        for (int i = 0; i < rounds; ++i)
        {
            BEAST_EXPECT(net.client.waitAndCheckStatus(
                finishHashes[i],
                totalReplay,
                TaskStatus::Completed,
                TaskStatus::Completed,
                deltaStatuses));
        }

        BEAST_EXPECT(
            net.client.waitForLedgers(finishHashes[0], totalReplay * rounds));
        BEAST_EXPECT(net.client.countsAsExpected(
            rounds, rounds, rounds * (totalReplay - 1)));

        // sweep
        net.client.replayer.sweep();
        BEAST_EXPECT(net.client.countsAsExpected(0, 0, 0));
    }
};

BEAST_DEFINE_TESTSUITE(LedgerReplay, app, ripple);
BEAST_DEFINE_TESTSUITE(LedgerReplayer, app, ripple);
BEAST_DEFINE_TESTSUITE(LedgerReplayerTimeout, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(LedgerReplayerLong, app, ripple);

}  // namespace test
}  // namespace ripple
