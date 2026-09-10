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
#include <xrpl/resource/Fees.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

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
     * Whether a packet has advanced the acquisition since the flag was last
     * cleared.
     *
     * @return Whether progress has been recorded.
     */
    [[nodiscard]] bool
    madeProgress() const
    {
        ScopedLockType const sl(mtx_);
        return progress_;
    }

    /**
     * Record that nothing is left to fetch.
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
     * The header as a liBASE reply, which is how an acquisition learns what it
     * is chasing.
     *
     * @param header The header to serialize.
     * @return The reply packet.
     */
    static std::shared_ptr<protocol::TMLedgerData>
    headerPacket(LedgerHeader const& header)
    {
        auto packet = std::make_shared<protocol::TMLedgerData>();
        packet->set_ledgerhash(header.hash.data(), uint256::size());
        packet->set_ledgerseq(header.seq);
        packet->set_type(protocol::liBASE);

        Serializer s;
        addRaw(header, s);

        auto* const node = packet->add_nodes();
        node->set_nodedata(s.peekData().data(), s.peekData().size());
        return packet;
    }

    /**
     * The chain's state-map nodes as a liAS_NODE reply for the given header.
     *
     * @param header The header whose hash and sequence the reply names.
     * @param chain The chain supplying the nodes.
     * @param data The nodes to include, each with its claimed position.
     * @return The reply packet.
     */
    static std::shared_ptr<protocol::TMLedgerData>
    stateNodePacket(
        LedgerHeader const& header,
        DeepChain const& chain,
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> const& data)
    {
        return packetFor(chain, data, protocol::liAS_NODE, header.hash, header.seq);
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
     * A ledger completed by a walk rather than by tryDB() is settled
     * before it is reported complete.
     *
     * The other order is what lets an unsettled ledger escape: isComplete()
     * is read without mtx_, so a second thread can act on it while done()
     * is still settling, and a mutable ledger reaching
     * LedgerHistory::insert() calls logicError(). The order itself is only
     * visible to a concurrent reader, so what this case pins is the
     * consequence - the acquisition never reports a ledger that is not
     * immutable - and that trigger() completes an acquisition without
     * itself setting the completion flag.
     *
     * @param env The environment to run in.
     */
    void
    testWalkSettlesBeforeReportingComplete(jtx::Env& env)
    {
        testcase("A ledger completed by a walk is settled before it is reported");

        auto const chain = DeepChain::toLeaf(2, nextSeed());
        auto const header = makeHeader(chain);

        // Everything except the leaf, so tryDB() can root the state map but its walk still has
        // something to ask for. That is what leaves the completion to trigger().
        storeHeader(env, header);
        storeStateNodes(env, header, chain, chain.deepestDepth - 1);

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        BEAST_EXPECT(!acquire->checkLocal());
        BEAST_EXPECT(!acquire->isComplete());
        BEAST_EXPECT(!acquire->isFailed());

        // Only now can the walk finish, so nothing but the walk can have completed this.
        storeStateNodes(env, header, chain, chain.deepestDepth);

        acquire->triggerAdded();

        BEAST_EXPECT(acquire->isComplete());
        BEAST_EXPECT(!acquire->isFailed());

        auto const settled = acquire->getLedger();
        BEAST_EXPECT(settled != nullptr);
        if (settled)
            BEAST_EXPECT(settled->isImmutable());

        // done()'s success arm ran, so the ledger reached LedgerMaster and nothing was recorded as
        // a failure.
        BEAST_EXPECT(env.app().getLedgerMaster().getLedgerByHash(header.hash) != nullptr);
        BEAST_EXPECT(!env.app().getInboundLedgers().isFailure(header.hash));
    }

    /**
     * A ledger whose map goes invalid on the way to being settled must be
     * discarded rather than delivered.
     *
     * done() settles the ledger before it logs or acts on the outcome, and a
     * map abandoned by then makes settling refuse, so the acquisition has to
     * record a failure instead. Reproduced by setting the flag and then
     * invalidating the map, which is the order a walk on another thread
     * produces without the second thread.
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

        // The state of affairs done() is handed: nothing left to fetch as far as the caller could
        // tell.
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

        // Nothing was handed to LedgerMaster, and the hash is remembered as a failure so it is not
        // immediately re-acquired.
        BEAST_EXPECT(env.app().getLedgerMaster().getLedgerByHash(header.hash) == nullptr);
        BEAST_EXPECT(waitFor([&] { return env.app().getInboundLedgers().isFailure(header.hash); }));
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
     * An acquisition whose state root names a shape no valid tree can have must
     * fail, and must cost the sender the harsher tier.
     *
     * @param env The environment to run in.
     */
    void
    testFabricatedChainFailsAcquire(jtx::Env& env)
    {
        testcase("A state-map chain reaching kLeafDepth fails the acquire");

        DeepChain const chain{nextSeed()};
        auto const header = makeHeader(chain);

        auto acquire = std::make_shared<TestableInboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        // The header is accepted on its own terms, so the acquisition now chases this hash.
        auto const headerPeer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(headerPeer, headerPacket(header)));
        acquire->runData();
        BEAST_EXPECT(headerPeer->charges().empty());
        BEAST_EXPECT(!acquire->isFailed());

        // The root, then the rest of the chain ending in the inner node at kLeafDepth.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data;
        data.emplace_back(SHAMapNodeID{}, chain.nodeAt(0));
        for (auto const& node : chain.nodesBelowRoot())
            data.push_back(node);

        // The header counted as progress, so clear it to see what the packet below records.
        acquire->clearProgress();

        auto const chainPeer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(chainPeer, stateNodePacket(header, chain, data)));
        acquire->runData();

        // The acquisition is over, and stays over: no peer can satisfy this hash.
        BEAST_EXPECT(acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());

        // The nodes ahead of the bad one belong to a tree that cannot exist, so the packet counts
        // nothing at all. Nothing else observes that tally, so without this the discarding could be
        // dropped and the suite would stay green.
        BEAST_EXPECT(!acquire->madeProgress());

        // A failed acquisition must not hand back the partial ledger it built.
        BEAST_EXPECT(acquire->getLedger() == nullptr);

        // Charged as data no honest peer sends by accident, not as merely-wrong data.
        BEAST_EXPECT(chainPeer->charges() == std::vector{resource::kFeeMalformedData});

        // getJson() walks the same maps to report what is still needed, and is reachable over RPC
        // for as long as sweep() keeps the failed entry. It must report the failure and come back
        // with nothing needed rather than descending the abandoned map.
        auto const report = acquire->getJson(0);
        BEAST_EXPECT(report[jss::failed].asBool());
        BEAST_EXPECT(report[jss::have_header].asBool());
        BEAST_EXPECT(!report[jss::have_state].asBool());
        BEAST_EXPECT(report[jss::needed_state_hashes].size() == 0);
    }

    /**
     * A merely-wrong state node must cost the recoverable tier and leave the
     * acquisition alive.
     *
     * The counterpart to testFabricatedChainFailsAcquire() on this path:
     * the fee split in receiveNode() turns on whether the map survived, so
     * a node that cannot be hooked but leaves the map sound must be
     * charged the lower tier.
     *
     * @param env The environment to run in.
     */
    void
    testWrongStateNodeKeepsAcquireAlive(jtx::Env& env)
    {
        testcase("A merely-wrong state node leaves the acquire recoverable");

        DeepChain const chain{nextSeed()};
        auto const header = makeHeader(chain);

        auto acquire = std::make_shared<InboundLedger>(
            env.app(),
            header.hash,
            header.seq,
            InboundLedger::Reason::GENERIC,
            stopwatch(),
            std::make_unique<RequestCountingPeerSet>());

        auto const headerPeer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(headerPeer, headerPacket(header)));
        acquire->runData();
        BEAST_EXPECT(!acquire->isFailed());

        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(
            rootPeer, stateNodePacket(header, chain, {{SHAMapNodeID{}, chain.nodeAt(0)}})));
        acquire->runData();
        BEAST_EXPECT(rootPeer->charges().empty());

        // nodeAt(1) is the node the root is missing and its hash matches, but we label it as
        // living at depth 2, so it cannot be hooked anywhere.
        auto const wrongPeer = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->gotData(
            wrongPeer,
            stateNodePacket(header, chain, {{SHAMapNodeID{2, uint256{}}, chain.nodeAt(1)}})));
        acquire->runData();

        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});

        // The map is sound, so the acquisition is still going and still holds its ledger.
        BEAST_EXPECT(!acquire->isFailed());
        BEAST_EXPECT(!acquire->isComplete());
        BEAST_EXPECT(acquire->getLedger() != nullptr);
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

        // The chain as the transaction root; an arbitrary hash, seeded nowhere, as the state root.
        auto const header = makeHeader(chain.rootHash.asUInt256(), uint256{99});
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

        // A failed acquisition must not hand back the partial ledger it built.
        BEAST_EXPECT(acquire->getLedger() == nullptr);
    }

    /**
     * The aggressive-retry branch of trigger() must judge a map the walk
     * abandoned.
     *
     * That branch reads an empty getNeededHashes() result as "nothing
     * left to fetch", and the walk it runs can reach the invalid verdict
     * itself once nodes resolve from local storage rather than from a
     * peer.
     *
     * The staging matters: tryDB() runs first and would shadow this guard
     * if it could resolve the whole chain, so only the root is local to
     * begin with - enough for the state map to hold a root, without which
     * neededHashes() reports the root as missing and never walks, but not
     * enough to reach the offending depth. Reaching the branch also needs
     * a timeout count above kLedgerBecomeAggressiveThreshold, which the
     * case records directly rather than waiting fifteen seconds for the
     * timer chain to raise it.
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

        // Only now does the rest of the chain become resolvable, so tryDB() cannot have judged it.
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

        // haveState_ is what pins this guard rather than the setImmutable() backstop in done(),
        // which also fails the acquire: without the guard the empty result reads as success, and
        // every have-flag is set on the way to that backstop.
        BEAST_EXPECT(!acquire->getJson(0)[jss::have_state].asBool());

        // The same branch with no header yet, which is the other arm of hasInvalidMap(): there is
        // no map to judge, and reading that as a verdict would fail an acquisition that has only
        // just started. getNeededHashes() has asked for the header, so the non-empty branch is the
        // right one and the acquisition stays alive.
        auto headerless = std::make_shared<TestableInboundLedger>(
            env.app(),
            uint256{7},
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

        // A failed acquisition holds no ledger to hand back, and done() remembered the hash.
        BEAST_EXPECT(acquire->getLedger() == nullptr);
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
        testWalkSettlesBeforeReportingComplete(env);
        testInvalidatedLedgerFailsInDone(env);
        testLocalFailureSignalsDone(env);
        testFabricatedChainFailsAcquire(env);
        testWrongStateNodeKeepsAcquireAlive(env);
        testLocalChainFailsAcquire(env);
        testAggressiveRetryJudgesLocalMap(env);

        // Last: the only case that waits out a whole timeout chain.
        testTimerRetriesThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(InboundLedger, app, xrpl);

}  // namespace xrpl::test
