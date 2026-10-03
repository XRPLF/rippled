#include <test/app/AcquireTestHelpers.h>
#include <test/jtx/Env.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl.pb.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * An acquisition that exposes the entry points its bases keep protected, so a
 * case can reach them through this subclass alone.
 */
struct TestableInboundLedger final : InboundLedger
{
    using InboundLedger::InboundLedger;

    /**
     * Look for the ledger locally, ask the peers being tracked for the rest,
     * and arm the timer, as InboundLedgers::acquire() does. That caller holds
     * its collection lock across init(), which releases it, so this stands in
     * with a lock of its own. The mutex is declared first, so it outlives the
     * lock.
     */
    void
    startAcquire()
    {
        std::recursive_mutex collectionMutex;
        ScopedLockType collectionLock(collectionMutex);
        init(collectionLock);
    }

    /**
     * Ask for more nodes, or judge what has been collected, as a fresh
     * acquisition does.
     */
    void
    triggerAdded()
    {
        trigger(nullptr, TriggerReason::Added);
    }
};

struct InboundLedger_test : public beast::unit_test::Suite
{
    /**
     * A retry interval short enough that a whole timeout chain costs a fraction
     * of a second. TimeoutCounter refuses anything at or below 10ms.
     */
    static constexpr auto kFastRetry = std::chrono::milliseconds{20};

    /**
     * A seed unique to this chain within the suite.
     *
     * The Env below is shared, and its node store, fetch packs and remembered
     * failures are all keyed by hash. A fresh seed per chain gives every chain
     * distinct hashes, so each case resolves only its own nodes.
     *
     * @return The seed.
     */
    [[nodiscard]] unsigned int
    nextSeed()
    {
        return ++seed_;
    }

    /**
     * A ledger header naming the given map roots, with a hash derived from the
     * fields, so an acquisition accepts the header as its own however the roots
     * are chosen.
     *
     * @param txHash The transaction map root. Zero means no transactions.
     * @param accountHash The state map root. Zero is a ledger no acquisition
     *        can finish.
     * @return The header, with its hash filled in.
     */
    static LedgerHeader
    makeHeader(UInt256 const& txHash, UInt256 const& accountHash)
    {
        LedgerHeader header;
        header.seq = 2;
        header.parentCloseTime = NetClock::time_point{};
        header.closeTime = NetClock::time_point{};
        header.closeTimeResolution = NetClock::duration{10};
        header.closeFlags = 0;
        header.txHash = txHash;
        header.accountHash = accountHash;
        header.hash = calculateLedgerHash(header);
        return header;
    }

    /**
     * The common shape: no transactions, so only the state map is in play.
     *
     * @param chain The chain whose root to name as the state hash.
     * @return The header, with its hash filled in.
     */
    static LedgerHeader
    makeHeader(DeepChain const& chain)
    {
        return makeHeader(UInt256{}, chain.rootHash.asUInt256());
    }

    /**
     * Put the header in the local store, which is the first place tryDB()
     * looks. The store keeps its entries, where a fetch pack hands each out
     * once, so more than one acquisition of the same ledger can find it.
     *
     * @param env The environment whose node store to seed.
     * @param header The header to store, keyed by its own hash.
     */
    static void
    storeHeader(jtx::Env& env, LedgerHeader const& header)
    {
        Serializer s;
        s.add32(HashPrefix::LedgerMaster);
        addRaw(header, s);

        env.app().getNodeFamily().db().store(
            NodeObjectType::Ledger, std::move(s.modData()), header.hash, header.seq);
    }

    /**
     * Put every node of a chain in the local store, so a state-map walk
     * resolves the whole map from the store.
     *
     * @param env The environment whose node store to seed.
     * @param header The header whose sequence the nodes are stored under.
     * @param chain The chain supplying the nodes.
     * @param maxDepth The deepest node to store, so a caller can leave a walk
     *        something to ask for.
     */
    static void
    storeStateNodes(
        jtx::Env& env,
        LedgerHeader const& header,
        DeepChain const& chain,
        unsigned int maxDepth)
    {
        auto& db = env.app().getNodeFamily().db();

        for (auto depth = 0u; depth <= maxDepth; ++depth)
        {
            db.store(
                NodeObjectType::AccountNode,
                chain.prefixedNodeAt(depth),
                chain.nodeAt(depth)->getHash().asUInt256(),
                header.seq);
        }
    }

    /**
     * The header alone as a liBASE reply, so takeHeader() alone judges the
     * packet.
     *
     * @param header The header to carry, serialized without a hash prefix, as
     *        takeHeader() expects.
     * @return The reply packet.
     */
    static std::shared_ptr<protocol::TMLedgerData>
    headerPacket(LedgerHeader const& header)
    {
        Serializer s;
        addRaw(header, s);

        auto packet = std::make_shared<protocol::TMLedgerData>();
        packet->set_ledgerhash(header.hash.data(), UInt256::size());
        packet->set_ledgerseq(header.seq);
        packet->set_type(protocol::liBASE);
        packet->add_nodes()->set_nodedata(s.peekData().data(), s.peekData().size());
        return packet;
    }

    /**
     * An acquisition started with nothing in the store for its hash, so it
     * waits on its peers.
     *
     * @param env The environment to run in.
     * @param header The header of the ledger to acquire.
     * @return The acquisition, started.
     */
    static std::shared_ptr<TestableInboundLedger>
    startPeerAcquire(jtx::Env& env, LedgerHeader const& header)
    {
        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());
        acquire->startAcquire();
        return acquire;
    }

    /**
     * A ledger whose maps all resolve locally finishes on the spot, and the
     * finished ledger is immutable and handed on.
     *
     * Both entry points into tryDB() are driven: InboundLedgers::acquire(), the
     * only caller of init(), and checkLocal(), which reaches done().
     *
     * @param env The environment to run in.
     */
    void
    testLocalLedgerCompletesAcquire(jtx::Env& env)
    {
        testcase("A ledger found locally completes the acquire");

        // A chain ending in a real leaf, so the state map is complete rather than merely rooted.
        // No transactions, so only the state map is in play.
        auto const chain = DeepChain::toLeaf(2, nextSeed());
        auto const header = makeHeader(chain);

        storeHeader(env, header);
        storeStateNodes(env, header, chain, chain.deepestDepth);

        // acquire() returns the ledger only once the acquisition is complete and unfailed, so a
        // non-null result shows tryDB() found it in the store.
        auto const acquired = env.app().getInboundLedgers().acquire(
            header.hash, header.seq, InboundLedger::Reason::GENERIC);

        BEAST_EXPECT(acquired != nullptr);
        if (acquired)
        {
            BEAST_EXPECT(acquired->isImmutable());
            BEAST_EXPECT(acquired->header().hash == header.hash);
        }

        // init() hands a ledger it completed to LedgerMaster itself, which is what makes it
        // available to everything else.
        BEAST_EXPECT(env.app().getLedgerMaster().getLedgerByHash(header.hash) != nullptr);

        // The failure list stays clear, which is the other arm of done().
        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));

        // The same ledger through checkLocal(), which unlike init() reaches done(). Everything it
        // needs is still in the store, since the first acquisition read rather than consumed it.
        auto again = std::make_shared<InboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        // True because the acquisition ended, which it reports only after done() has run.
        BEAST_EXPECT(again->checkLocal());
        BEAST_EXPECT(again->isComplete());
        BEAST_EXPECT(!again->isFailed());

        auto const settled = again->getLedger();
        BEAST_EXPECT(settled != nullptr);
        if (settled)
            BEAST_EXPECT(settled->isImmutable());

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));
    }

    /**
     * An acquisition that fails on local data must still signal.
     *
     * Both entry points that reach tryDB() are covered. done() is what
     * signals, runs logFailure(), and lands the hash in recentFailures_,
     * which stops the next round asking for the same ledger.
     * recentFailures_ is what the assertions watch, since it is the
     * caller-visible consequence of having signaled.
     *
     * @param env The environment to run in.
     */
    void
    testLocalFailureSignalsDone(jtx::Env& env)
    {
        testcase("An acquisition that fails locally still signals");

        // A zero account hash is a ledger no acquisition can ever finish, and tryDB() says so as
        // soon as it has the header.
        auto const header = makeHeader(UInt256{}, UInt256{});
        storeHeader(env, header);

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));

        // acquire() is the only caller of init(), and returns nullptr for a failed acquisition.
        BEAST_EXPECT(
            env.app().getInboundLedgers().acquire(
                header.hash, header.seq, InboundLedger::Reason::GENERIC) == nullptr);

        // The failure reached recentFailures_, which is what stops the next round asking again.
        BEAST_EXPECT(waitFor([&] { return env.app().getInboundLedgers().isFailure(header.hash); }));

        // The other way tryDB() judges a ledger unobtainable: the header it found is not the one
        // asked for. A non-zero account hash, so the route above cannot be what fails this one.
        auto const strayHeader = makeHeader(UInt256{}, UInt256{2});
        storeHeader(env, strayHeader);

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(strayHeader.hash));

        // acquire() hands the sequence to the acquisition unscreened, so one past the stored
        // header's is enough to make tryDB() reject what it reads.
        auto const wrongSeq = strayHeader.seq + 1;

        BEAST_EXPECT(
            env.app().getInboundLedgers().acquire(
                strayHeader.hash, wrongSeq, InboundLedger::Reason::GENERIC) == nullptr);

        // tryDB() discards the ledger it built before it gives up, so done() runs here with none
        // to report. The failure is recorded all the same, which is what the call is for.
        BEAST_EXPECT(
            waitFor([&] { return env.app().getInboundLedgers().isFailure(strayHeader.hash); }));

        // The other route into tryDB(): a trigger() on an acquisition that has no header yet. A
        // hash of its own, so this drives a fresh entry.
        auto const otherHeader = makeHeader(UInt256{1}, UInt256{});
        storeHeader(env, otherHeader);

        auto viaTrigger = std::make_shared<TestableInboundLedger>(
            env.app(),
            otherHeader.hash,
            otherHeader.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(otherHeader.hash));

        viaTrigger->triggerAdded();

        // Failed before any header state was published: no ledger, and have_header false.
        BEAST_EXPECT(viaTrigger->isFailed());
        BEAST_EXPECT(!viaTrigger->isComplete());
        BEAST_EXPECT(viaTrigger->getLedger() == nullptr);
        BEAST_EXPECT(!viaTrigger->getJson(0)[jss::have_header].asBool());
        BEAST_EXPECT(
            waitFor([&] { return env.app().getInboundLedgers().isFailure(otherHeader.hash); }));
    }

    /**
     * A peer-supplied header whose account hash is zero fails the acquisition
     * and signals, as a local one does. The peer is not charged, and
     * recentFailures_ is what shows done() ran.
     *
     * @param env The environment to run in.
     */
    void
    testPeerZeroAccountHashFails(jtx::Env& env)
    {
        testcase("A peer header with a zero account hash fails the acquire");

        // A real transaction root and a zero state root. Nothing is stored for this hash, so only a
        // peer can supply the header.
        auto const chain = DeepChain::toLeaf(2, nextSeed());
        auto const header = makeHeader(chain.rootHash.asUInt256(), UInt256{});

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));

        auto acquire = startPeerAcquire(env, header);
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_header].asBool());

        auto const peer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(peer, headerPacket(header)));
        acquire->runData();

        // Failed, not complete, and the header is dropped, as tryDB() leaves one.
        BEAST_EXPECT(acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());
        BEAST_EXPECT(acquire->getLedger() == nullptr);
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_header].asBool());

        // The header is the one asked for, so the sender is not to blame.
        BEAST_EXPECT(peer->charges().empty());

        // done() ran, so the hash reached recentFailures_, which stops the next round asking again.
        BEAST_EXPECT(waitFor([&] { return env.app().getInboundLedgers().isFailure(header.hash); }));
    }

    /**
     * A peer-supplied header with a zero transaction hash is taken, since that
     * is an empty transaction set, and the acquisition finishes once the peer
     * supplies the state map.
     *
     * @param env The environment to run in.
     */
    void
    testPeerHeaderWithoutTransactionsCompletes(jtx::Env& env)
    {
        testcase("A peer header with no transactions completes the acquire");

        // No transactions, and a state map that ends in a real leaf, so the acquisition can finish.
        auto const chain = DeepChain::toLeaf(2, nextSeed());
        auto const header = makeHeader(chain);

        auto acquire = startPeerAcquire(env, header);

        auto const peer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(peer, headerPacket(header)));
        acquire->runData();

        // The header is taken, the empty transaction map counts as fetched, and the state map is
        // what remains.
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());
        auto const json = acquire->getJson(0);
        BEAST_EXPECT(json[jss::have_header].asBool());
        BEAST_EXPECT(json[jss::have_transactions].asBool());
        BEAST_EXPECT(!json[jss::have_state].asBool());

        // The whole state map as state-node replies, root first, which completes the acquisition.
        // The second gotData() answers false, since the first already asked for a dispatch.
        BEAST_EXPECT(acquire->gotData(
            peer,
            packetFor(
                chain,
                {{SHAMapNodeID{}, chain.nodeAt(0)}},
                protocol::liAS_NODE,
                header.hash,
                header.seq)));
        BEAST_EXPECT(!acquire->gotData(
            peer,
            packetFor(
                chain, chain.nodesBelowRoot(), protocol::liAS_NODE, header.hash, header.seq)));
        acquire->runData();

        BEAST_EXPECT(acquire->isComplete());
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(peer->charges().empty());

        auto const settled = acquire->getLedger();
        BEAST_EXPECT(settled != nullptr);
        if (settled)
            BEAST_EXPECT(settled->isImmutable());

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));
    }

    /**
     * The retry timer re-asks, then gives up and signals.
     *
     * The only case that drives onTimer() rather than trigger() directly,
     * which is what covers the give-up: past kLedgerTimeoutRetriesMax the
     * acquisition fails itself and done() records that, so the same
     * doomed ledger is not asked for again on the next round. It is also
     * what the retry interval is a constructor parameter for, since the
     * chain runs past kLedgerTimeoutRetriesMax ticks of three seconds
     * apiece in production.
     *
     * The store is empty for this hash and no reply arrives, so every tick
     * counts a timeout and the count climbs to the limit. A hash of its own,
     * so this case owns its entry in the failure list.
     *
     * @param env The environment to run in.
     */
    void
    testTimerRetriesThenGivesUp(jtx::Env& env)
    {
        testcase("The retry timer re-asks, then gives up");

        UInt256 const kUnknownLedger{8};

        // One candidate, which onTimer() re-offers on every tick.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            kUnknownLedger,
            0,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::move(peerSet),
            kFastRetry);

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(kUnknownLedger));

        // init() finds the hash absent from the store, so it asks the candidate and arms the retry
        // timer. The case counts from here, past those first requests.
        acquire->startAcquire();
        BEAST_EXPECT(!acquire->isFailed());
        int const requestsFromInit = peerSetPtr->requests();
        BEAST_EXPECT(requestsFromInit > 0);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::ID>{candidate->id()});

        // Every tick asks again, and past kLedgerTimeoutRetriesMax (6) the chain gives up.
        BEAST_EXPECT(waitFor([&] { return acquire->isFailed(); }));
        BEAST_EXPECT(!acquire->isComplete());
        BEAST_EXPECT(peerSetPtr->requests() > requestsFromInit);

        // done() remembered the hash, which is what stops the next round asking again.
        BEAST_EXPECT(
            waitFor([&] { return env.app().getInboundLedgers().isFailure(kUnknownLedger); }));
    }

    void
    run() override
    {
        // One Env for the suite, since building one costs far more than any case here. Safe
        // because every chain is seeded through nextSeed(): the node store, the fetch packs and
        // the remembered failures are all shared, and all three are keyed by hash.
        jtx::Env env{*this};

        testLocalLedgerCompletesAcquire(env);
        testLocalFailureSignalsDone(env);
        testPeerZeroAccountHashFails(env);
        testPeerHeaderWithoutTransactionsCompletes(env);

        // Last: the only case that waits out a whole timeout chain.
        testTimerRetriesThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(InboundLedger, app, xrpl);

}  // namespace xrpl::test
