#include <test/app/AcquireTestHelpers.h>
#include <test/jtx/Env.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

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

    /**
     * The same, as the timer chain does.
     */
    void
    triggerTimeout()
    {
        trigger(nullptr, TriggerReason::Timeout);
    }

    /**
     * Record how many timeouts have elapsed.
     *
     * @param timeouts The count to record.
     */
    void
    setTimeouts(int timeouts)
    {
        ScopedLockType const sl(mtx_);
        timeouts_ = timeouts;
    }

    /**
     * Forget any recorded progress.
     */
    void
    clearProgress()
    {
        ScopedLockType const sl(mtx_);
        progress_ = false;
    }

    /**
     * Record that every part has been fetched.
     */
    void
    markComplete()
    {
        ScopedLockType const sl(mtx_);
        complete_ = true;
    }

    /**
     * Settle the acquisition and signal whatever is waiting on it.
     */
    void
    signalDone()
    {
        ScopedLockType const sl(mtx_);
        done();
    }
};

/**
 * The ledger an acquisition is assembling, as a pointer that can modify it.
 *
 * @param acquire The acquisition to read from.
 * @return The ledger, or nullptr if there is none to report.
 */
[[nodiscard]] static std::shared_ptr<Ledger>
mutableLedger(InboundLedger const& acquire)
{
    // Sound because the acquisition holds a non-const ledger and only hands out a const view.
    return std::const_pointer_cast<Ledger>(acquire.getLedger());
}

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

        // init() hands a ledger it completed to LedgerMaster itself.
        BEAST_EXPECT(env.app().getLedgerMaster().getLedgerByHash(header.hash) != nullptr);

        // The failure list stays clear, which is the other arm of done().
        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));

        // The reason decides where a settled ledger goes: HISTORY counts it toward the fetch rate
        // instead of handing it to LedgerMaster.
        {
            auto const historyChain = DeepChain::toLeaf(2, nextSeed());
            auto const historyHeader = makeHeader(historyChain);

            storeHeader(env, historyHeader);
            storeStateNodes(env, historyHeader, historyChain, historyChain.deepestDepth);

            // onLedgerFetched() is the only writer of this rate, and done() calls it before it
            // posts any work, so the read below is not racing the job queue.
            auto const rateBefore = env.app().getInboundLedgers().fetchRate();

            auto history = std::make_shared<InboundLedger>(
                env.app(),
                historyHeader.hash,
                historyHeader.seq,
                InboundLedger::Reason::HISTORY,
                stopwatch(),
                std::make_unique<RequestCountingPeerSet>());

            BEAST_EXPECT(history->checkLocal());
            BEAST_EXPECT(history->isComplete());
            BEAST_EXPECT(!history->isFailed());

            // The switch the rate check reads is reached only once the ledger has been settled.
            auto const historyLedger = history->getLedger();
            BEAST_EXPECT(historyLedger != nullptr);
            if (historyLedger)
                BEAST_EXPECT(historyLedger->isImmutable());

            BEAST_EXPECT(env.app().getInboundLedgers().fetchRate() > rateBefore);
        }

        // The same ledger through checkLocal(), which unlike init() reaches done().
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
     * A ledger whose map goes invalid on the way to being settled must be
     * discarded rather than delivered.
     *
     * done() settles the ledger before it logs or acts on the outcome, and an
     * abandoned map makes settling refuse, so the acquisition records a
     * failure.
     *
     * @param env The environment to run in.
     */
    void
    testInvalidatedLedgerFailsInDone(jtx::Env& env)
    {
        testcase("A ledger invalidated on its way to being settled fails");

        // The fabricated chain, so feeding it to the state map invalidates the map.
        DeepChain const chain{nextSeed()};

        // Only the header is local, so the acquisition holds a ledger with an empty state map.
        auto const header = makeHeader(chain);
        storeHeader(env, header);

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        BEAST_EXPECT(!acquire->checkLocal());
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());

        auto const ledger = mutableLedger(*acquire);
        BEAST_EXPECT(ledger != nullptr);
        if (!ledger)
            return;

        // The state of affairs done() is handed: every part fetched, as far as the caller can tell.
        acquire->markComplete();

        // And the walk that has since reached the verdict.
        auto& stateMap = ledger->stateMap();
        BEAST_EXPECT(stateMap.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
        for (auto const& [nodeID, node] : chain.nodesBelowRoot())
            stateMap.addKnownNode(nodeID, node, nullptr);
        BEAST_EXPECT(!stateMap.isValid());

        acquire->signalDone();

        // complete_ is withdrawn alongside the failure, or every guard that checks it before
        // failed_ keeps treating this ledger as delivered.
        BEAST_EXPECT(!acquire->isComplete());
        BEAST_EXPECT(acquire->isFailed());

        // getLedgerByHash answers null for the hash, and the hash is remembered as a failure,
        // which defers re-acquisition.
        BEAST_EXPECT(env.app().getLedgerMaster().getLedgerByHash(header.hash) == nullptr);
        BEAST_EXPECT(waitFor([&] { return env.app().getInboundLedgers().isFailure(header.hash); }));
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

        BEAST_EXPECT(viaTrigger->isFailed());
        BEAST_EXPECT(!viaTrigger->isComplete());
        BEAST_EXPECT(
            waitFor([&] { return env.app().getInboundLedgers().isFailure(otherHeader.hash); }));
    }

    /**
     * A ledger assembled from local data must be judged even when only
     * one map is settled.
     *
     * tryDB() walks both maps to see what is on hand, and a fetch pack is
     * checked against each node's own hash rather than the shape it
     * implies, so a whole chain can resolve locally without passing
     * through addKnownNode().
     *
     * The asymmetry is the point: the transaction map is the chain, so
     * its walk abandons it, while the state root is a hash no fetch pack
     * supplies, leaving that map merely incomplete. tryDB() therefore
     * sets neither flag and has to reach the verdict itself, since the
     * setImmutable() call further down needs both.
     *
     * @param env The environment to run in.
     */
    void
    testLocalChainFailsAcquire(jtx::Env& env)
    {
        testcase("A chain found locally fails the acquire");

        DeepChain const chain{nextSeed()};

        // The chain as the transaction root, and an arbitrary hash, seeded nowhere, as the state
        // root.
        auto const header = makeHeader(chain.rootHash.asUInt256(), UInt256{99});
        auto& ledgerMaster = env.app().getLedgerMaster();

        // The header, prefixed the way tryDB() expects to find it in a fetch pack.
        Serializer hs;
        hs.add32(HashPrefix::LedgerMaster);
        addRaw(header, hs);
        ledgerMaster.addFetchPack(header.hash, std::make_shared<Blob>(hs.modData()));

        // Every node of the chain, keyed by its own hash. TransactionStateSF::getNode() reads
        // these, so the transaction-map walk resolves the whole chain with no peer involved.
        for (auto depth = 0u; depth <= SHAMap::kLeafDepth; ++depth)
        {
            ledgerMaster.addFetchPack(
                chain.nodeAt(depth)->getHash().asUInt256(),
                std::make_shared<Blob>(chain.prefixedNodeAt(depth)));
        }

        auto acquire = std::make_shared<InboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        // checkLocal() routes into tryDB() without any peer data having arrived. It reports true
        // only because the acquisition ended, which is what this case is about.
        BEAST_EXPECT(acquire->checkLocal());

        BEAST_EXPECT(acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());
    }

    /**
     * The aggressive-retry branch of trigger() must judge a map the walk
     * abandoned.
     *
     * That branch reads an empty getNeededHashes() result as a complete map,
     * and the walk it runs can reach the invalid verdict itself once nodes
     * resolve from local storage. Only the root is local when tryDB() runs, so
     * the state map holds a root and the walk stops one level down. The branch
     * also needs a timeout count above kLedgerBecomeAggressiveThreshold, which
     * the case records directly rather than waiting out the timer chain.
     *
     * @param env The environment to run in.
     */
    void
    testAggressiveRetryJudgesLocalMap(jtx::Env& env)
    {
        testcase("An aggressive retry judges a map the walk abandoned");

        DeepChain const chain{nextSeed()};

        // The chain as the state root, and no transactions, so only the state map is in play.
        auto const header = makeHeader(chain);
        auto& ledgerMaster = env.app().getLedgerMaster();

        Serializer hs;
        hs.add32(HashPrefix::LedgerMaster);
        addRaw(header, hs);
        ledgerMaster.addFetchPack(header.hash, std::make_shared<Blob>(hs.modData()));

        // Only the root, so the state map gets a root but the walk stops one level down.
        ledgerMaster.addFetchPack(
            chain.nodeAt(0)->getHash().asUInt256(),
            std::make_shared<Blob>(chain.prefixedNodeAt(0)));

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        // The acquisition is alive: it has the header and a state root, and still wants the rest.
        BEAST_EXPECT(!acquire->checkLocal());
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(acquire->getJson(0)[jss::have_header].asBool());
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_state].asBool());

        auto const ledger = mutableLedger(*acquire);
        BEAST_EXPECT(ledger != nullptr);
        if (!ledger)
            return;
        BEAST_EXPECT(ledger->stateMap().isValid());

        // The rest of the chain becomes resolvable only now, which places the verdict in this walk.
        for (auto depth = 1u; depth <= SHAMap::kLeafDepth; ++depth)
        {
            ledgerMaster.addFetchPack(
                chain.nodeAt(depth)->getHash().asUInt256(),
                std::make_shared<Blob>(chain.prefixedNodeAt(depth)));
        }

        // kLedgerBecomeAggressiveThreshold is 4 and file-local, so name the requirement here.
        acquire->setTimeouts(5);
        acquire->clearProgress();
        acquire->triggerTimeout();

        // The walk resolved the chain locally and abandoned the map, and trigger() recorded that
        // rather than reading the empty result as a finished acquisition.
        BEAST_EXPECT(!ledger->stateMap().isValid());
        BEAST_EXPECT(acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());

        // have_state is the discriminating assertion: only this guard leaves it false, since every
        // have-flag is set on the way to the setImmutable() backstop in done().
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_state].asBool());

        // The same branch with no header, which is the other arm of hasInvalidMap(): no map, so
        // the arm answers false. getNeededHashes() has asked for the header, so the non-empty
        // branch is the right one and the acquisition stays alive.
        auto headerless = std::make_shared<TestableInboundLedger>(
            env.app(),
            UInt256{7},
            0,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        headerless->setTimeouts(5);
        headerless->clearProgress();
        headerless->triggerTimeout();

        BEAST_EXPECT(mutableLedger(*headerless) == nullptr);
        BEAST_EXPECT(!headerless->isFailed());
        BEAST_EXPECT(!headerless->isComplete());
    }

    /**
     * The ordinary trigger() path must judge a map its own walk abandoned.
     *
     * Covers the state-map walk trigger() runs with mtx_ released, where the
     * case above covers the empty getNeededHashes() branch. That verdict has to
     * be read before the flags below it. Staged as that case is, with the
     * timeout count left at zero to stay off the aggressive branch.
     *
     * @param env The environment to run in.
     */
    void
    testWalkJudgesMapOnOrdinaryTrigger(jtx::Env& env)
    {
        testcase("An ordinary trigger judges a map its walk abandoned");

        DeepChain const chain{nextSeed()};

        auto const header = makeHeader(chain);
        auto& ledgerMaster = env.app().getLedgerMaster();

        Serializer hs;
        hs.add32(HashPrefix::LedgerMaster);
        addRaw(header, hs);
        ledgerMaster.addFetchPack(header.hash, std::make_shared<Blob>(hs.modData()));

        // Only the root, so the state map gets a root but the walk stops one level down.
        ledgerMaster.addFetchPack(
            chain.nodeAt(0)->getHash().asUInt256(),
            std::make_shared<Blob>(chain.prefixedNodeAt(0)));

        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::move(peerSet));

        BEAST_EXPECT(!acquire->checkLocal());
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(acquire->getJson(0)[jss::have_header].asBool());
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_state].asBool());

        auto const ledger = mutableLedger(*acquire);
        BEAST_EXPECT(ledger != nullptr);
        if (!ledger)
            return;
        BEAST_EXPECT(ledger->stateMap().isValid());

        // The rest of the chain becomes resolvable only now, which places the verdict in this walk.
        for (auto depth = 1u; depth <= SHAMap::kLeafDepth; ++depth)
        {
            ledgerMaster.addFetchPack(
                chain.nodeAt(depth)->getHash().asUInt256(),
                std::make_shared<Blob>(chain.prefixedNodeAt(depth)));
        }

        // Sound going in, which is the first half of the tripwire below.
        BEAST_EXPECT(ledger->stateMap().isValid());

        int const requestsBefore = peerSetPtr->requests();
        acquire->triggerAdded();

        // The walk resolved the chain locally and abandoned the map, and trigger() recorded that
        // rather than reading the empty node list as a finished state map.
        BEAST_EXPECT(!ledger->stateMap().isValid());
        BEAST_EXPECT(acquire->isFailed());

        // have_state is the discriminating assertion. isComplete() is not: it reads false whether
        // this guard fired or the setImmutable() backstop in done() did.
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_state].asBool());

        // A tripwire that keeps this case on the branch it names: the aggressive branch needs both
        // a TIMEOUT reason and a count above kLedgerBecomeAggressiveThreshold.
        BEAST_EXPECT(acquire->getJson(0)[jss::timeouts].asInt() == 0);

        // The request count is unchanged after the verdict, so the guard ended the round.
        BEAST_EXPECT(peerSetPtr->requests() == requestsBefore);
    }

    /**
     * The retry timer re-asks, then gives up and signals.
     *
     * The only case that drives onTimer() rather than trigger() directly. Past
     * kLedgerTimeoutRetriesMax the acquisition fails itself and done() records
     * that, so the same ledger is not asked for again. The store is empty for
     * this hash and no reply arrives, so every tick counts a timeout.
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
        testInvalidatedLedgerFailsInDone(env);
        testLocalFailureSignalsDone(env);
        testLocalChainFailsAcquire(env);
        testAggressiveRetryJudgesLocalMap(env);
        testWalkJudgesMapOnOrdinaryTrigger(env);

        // Last: the only case that waits out a whole timeout chain.
        testTimerRetriesThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(InboundLedger, app, xrpl);

}  // namespace xrpl::test
