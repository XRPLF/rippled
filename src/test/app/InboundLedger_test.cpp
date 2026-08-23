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

#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * An acquisition that exposes the entry points its bases keep protected, so a
 * case can reach them without the daemon's API growing.
 */
struct TestableInboundLedger final : InboundLedger
{
    using InboundLedger::InboundLedger;

    /**
     * Look for the ledger locally, ask the peers being tracked for the
     * rest, and arm the timer, as InboundLedgers::acquire() does.
     *
     * That caller holds its collection lock across init(), which releases
     * it, so this stands in with a lock of its own. Declared before the
     * lock, so it outlives it.
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
     * A seed no other chain in this suite has used.
     *
     * The Env below is shared, and its node store, fetch packs and remembered
     * failures are all keyed by hash, so two cases building identically seeded
     * chains would let one resolve or judge the other's. Handing out a fresh
     * seed per chain makes that impossible rather than merely unlikely.
     *
     * @return The seed.
     */
    [[nodiscard]] unsigned int
    nextSeed()
    {
        return ++seed_;
    }

    /**
     * A ledger header naming the given map roots.
     *
     * The hash is derived from the fields, so an acquisition accepts the
     * header as its own however the roots are chosen.
     *
     * @param txHash The transaction map root; zero means no transactions.
     * @param accountHash The state map root; zero is a ledger no
     *        acquisition can finish.
     * @return The header, with its hash filled in.
     */
    static LedgerHeader
    makeHeader(uint256 const& txHash, uint256 const& accountHash)
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
        return makeHeader(uint256{}, chain.rootHash.asUInt256());
    }

    /**
     * Put the header in the local store, which is the first place tryDB()
     * looks.
     *
     * Unlike a fetch pack, which hands each entry out once, the store
     * keeps it, so more than one acquisition of the same ledger can find
     * it.
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
     * resolves the whole map without a peer.
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
     * A ledger whose maps all resolve locally finishes on the spot, and the
     * finished ledger is immutable and handed on.
     *
     * The case where tryDB() alone completes the acquisition, so it covers
     * tryDB() reporting a ledger it found and done() taking its success arm
     * on that path. Both entry points are driven: InboundLedgers::acquire()
     * is the only caller of init(), and hands back the finished ledger
     * itself, while checkLocal() is the route that reaches done().
     *
     * @param env The environment to run in.
     */
    void
    testLocalLedgerCompletesAcquire(jtx::Env& env)
    {
        testcase("A ledger found locally completes the acquire");

        // A chain ending in a real leaf, so the state map is genuinely complete rather than merely
        // rooted. No transactions, so only the state map is in play.
        auto const chain = DeepChain::toLeaf(2, nextSeed());
        auto const header = makeHeader(chain);

        storeHeader(env, header);
        storeStateNodes(env, header, chain, chain.deepestDepth);

        // acquire() runs init() under its own collection lock and returns the ledger only once the
        // acquisition is complete and unfailed, so a non-null result is what shows tryDB() found it
        // without a peer ever being asked.
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

        // Nothing was logged as a failure, which is the other arm of done().
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

        // True because the acquisition ended, which here means it succeeded, and it reports that
        // only after done() has run.
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
     * Both entry points that reach tryDB() are covered, since without
     * done() the object never signals, logFailure() never runs, and the
     * hash never lands in recentFailures_ - so the same doomed ledger is
     * asked for again on the next round. recentFailures_ is what the
     * assertions watch, since it is the caller-visible consequence of
     * having signalled.
     *
     * @param env The environment to run in.
     */
    void
    testLocalFailureSignalsDone(jtx::Env& env)
    {
        testcase("An acquisition that fails locally still signals");

        // A zero account hash is a ledger no acquisition can ever finish, and tryDB() says so as
        // soon as it has the header.
        auto const header = makeHeader(uint256{}, uint256{});
        storeHeader(env, header);

        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));

        // acquire() is the only caller of init(), and hands back nothing for a failed acquisition.
        BEAST_EXPECT(
            env.app().getInboundLedgers().acquire(
                header.hash, header.seq, InboundLedger::Reason::GENERIC) == nullptr);

        // The failure reached recentFailures_, which is what stops the next round asking again.
        BEAST_EXPECT(waitFor([&] { return env.app().getInboundLedgers().isFailure(header.hash); }));

        // The other route into tryDB(): a trigger() on an acquisition that has no header yet. A
        // hash of its own, so the entry above cannot answer for it.
        auto const otherHeader = makeHeader(uint256{1}, uint256{});
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

        BEAST_EXPECT(viaTrigger->isFailed());
        BEAST_EXPECT(!viaTrigger->isComplete());
        BEAST_EXPECT(
            waitFor([&] { return env.app().getInboundLedgers().isFailure(otherHeader.hash); }));
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
     * Nothing is local and no data ever arrives, so no tick can record progress
     * and the count only climbs. A hash of its own, so no other case can have
     * remembered it as a failure already.
     *
     * @param env The environment to run in.
     */
    void
    testTimerRetriesThenGivesUp(jtx::Env& env)
    {
        testcase("The retry timer re-asks, then gives up");

        uint256 const kUnknownLedger{8};

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

        // init() finds nothing locally, so it asks the candidate and queues the first check-in,
        // which is what arms the retry timer for every cycle after. Those first requests are not
        // the ones under test, so count from here.
        acquire->startAcquire();
        BEAST_EXPECT(!acquire->isFailed());
        int const requestsFromInit = peerSetPtr->requests();
        BEAST_EXPECT(requestsFromInit > 0);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::id_t>{candidate->id()});

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

        // Last: the only case that waits out a whole timeout chain.
        testTimerRetriesThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(InboundLedger, app, xrpl);

}  // namespace xrpl::test
