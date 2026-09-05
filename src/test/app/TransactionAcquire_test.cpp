#include <test/app/AcquireTestHelpers.h>
#include <test/jtx/Env.h>

#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl.pb.h>

#include <chrono>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * An acquisition that exposes the state its bases keep protected, so a case can
 * reach it without the daemon's API growing.
 */
struct TestableTransactionAcquire final : TransactionAcquire
{
    using TransactionAcquire::TransactionAcquire;

    /**
     * Whether the set being acquired is still structurally coherent.
     *
     * @return Whether the map is still valid.
     */
    [[nodiscard]] bool
    isMapValid() const
    {
        // Under the lock: a batch on another thread can reach the verdict.
        ScopedLockType const sl(mtx_);
        return map_->isValid();
    }

    /**
     * Whether a batch has advanced the set since the flag was last cleared.
     *
     * @return Whether progress has been recorded.
     */
    [[nodiscard]] bool
    madeProgress() const
    {
        // Under the lock: a timer tick clears the flag on a job thread.
        ScopedLockType const sl(mtx_);
        return progress_;
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
};

struct TransactionAcquire_test : public beast::unit_test::Suite
{
    /**
     * A retry interval short enough that a whole timeout chain costs a fraction
     * of a second.
     *
     * TimeoutCounter refuses anything at or below 10ms. At this interval the
     * window between the first retry (four timeouts in) and giving up (twenty)
     * is still a third of a second, which is what the one case that watches
     * both needs.
     */
    static constexpr auto kFastRetry = std::chrono::milliseconds{20};

    /**
     * A seed no other chain in this suite has used.
     *
     * The Env below is shared, and ConsensusTransSetSF::gotNode() puts
     * every node it accepts into the application-wide NodeCache while
     * InboundTransactions keys its acquisitions by set hash, so two cases
     * building identically seeded chains would let one resolve or revive
     * the other's. Handing out a fresh seed per chain makes that
     * impossible rather than merely unlikely.
     *
     * @return The seed.
     */
    [[nodiscard]] unsigned int
    nextSeed()
    {
        return ++seed_;
    }

    /**
     * Whether takeNodes() declined to look at the data at all.
     *
     * TimeoutCounter::complete_ and failed_ are both protected, so this stands in
     * for either: a done acquisition returns a verdict accounting for nothing.
     *
     * @param san The verdict a takeNodes() call returned.
     * @return Whether that verdict shows the data was never looked at.
     */
    static bool
    wasIgnored(SHAMapAddNode const& san)
    {
        return tallyIs(san, 0, 0, 0);
    }

    /**
     * Wait for a finished set to reach InboundTransactions.
     *
     * done() hands the map over through a job, so this is what shows an
     * acquisition completed rather than merely stopped.
     *
     * @param env The environment whose InboundTransactions to watch.
     * @param setHash The set to wait for.
     * @return The delivered map, or nullptr if none arrived.
     */
    [[nodiscard]] static std::shared_ptr<SHAMap>
    waitForDeliveredSet(jtx::Env& env, uint256 const& setHash)
    {
        auto& inbound = env.app().getInboundTransactions();

        // acquire=false: asking to acquire would spin up a second, unrelated acquisition for
        // this hash (or, if one is already registered, needlessly poke stillNeed() on it).
        //
        // A shorter deadline than the polling default: the job is queued before takeNodes()
        // returns, so anything beyond a few seconds means it is never coming.
        std::shared_ptr<SHAMap> delivered;
        if (!waitFor(
                [&] { return (delivered = inbound.getSet(setHash, false)) != nullptr; },
                std::chrono::seconds{5}))
            return nullptr;
        return delivered;
    }

    /**
     * A chain ending in a real leaf completes the acquisition, and later
     * replies for it are then left alone.
     *
     * Also pins the two things that follow from finishing: nothing more
     * is asked for, and done() hands the map to InboundTransactions,
     * which is what consensus is waiting on.
     *
     * @param env The environment to run in.
     */
    void
    testHappyPathCompletesAcquisition(jtx::Env& env)
    {
        testcase("A chain ending in a leaf completes the acquire");

        auto const chain = DeepChain::toLeaf(3, nextSeed());

        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        uint256 const setHash = chain.rootHash.asUInt256();
        auto const acquire =
            std::make_shared<TransactionAcquire>(env.app(), setHash, std::move(peerSet));
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        // The root alone leaves the set incomplete, so accepting it asks for the level
        // below.
        auto const rootResult = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(rootResult.isUseful());
        int const requestsWhileIncomplete = peerSetPtr->requests();
        BEAST_EXPECT(requestsWhileIncomplete > 0);

        // The rest of the chain, ending in the leaf.
        auto const result = acquire->takeNodes(chain.nodesBelowRoot(), peer);
        BEAST_EXPECT(result.isUseful());
        BEAST_EXPECT(!result.isInvalid());

        // Nothing more went out, which rules out the acquisition still asking for nodes.
        // trigger() also sends nothing when it gives up, so the delivered set below is what
        // shows it finished.
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileIncomplete);

        // done() hands the map over only when it has not failed, so this is what separates
        // completion from failure.
        auto const delivered = waitForDeliveredSet(env, setHash);
        BEAST_EXPECT(delivered != nullptr);
        if (delivered)
        {
            BEAST_EXPECT(delivered->getHash() == chain.rootHash);
            BEAST_EXPECT(delivered->isValid());
        }

        // A reply arriving after the set is finished is not examined, and does not restart the
        // asking - the ordinary fate of every responder to trigger()'s broadcast but the one that
        // completed the set. init() was never called, so no timer can have failed the acquisition
        // since the set above was delivered.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileIncomplete);
    }

    /**
     * Two peers each answering with a different missing piece are both accepted
     * without penalty, and the set completes from their combined replies.
     *
     * Driven through InboundTransactions::gotData() so the leaf goes over the
     * real dispatch, which rebuilds a leaf's position from its own key rather
     * than trusting the sender's label.
     *
     * @param env The environment to run in.
     */
    void
    testTwoPeersEachSupplyPartOfTheSet(jtx::Env& env)
    {
        testcase("Two peers each supplying part of a set are both accepted without penalty");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        // getSet() with acquire=true registers the TransactionAcquire that gotData() then
        // looks up by hash.
        uint256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // The first peer answers with the root only.
        auto const peerA = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peerA, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(peerA->charges().empty());

        // The second answers with everything the first left out, and finishes the set.
        auto const peerB = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peerB, packetFor(chain, chain.nodesBelowRoot()));
        BEAST_EXPECT(peerB->charges().empty());

        // Completion does not depend on which peer sent which piece: the set finishes just
        // as it does when one peer supplies the lot.
        auto const delivered = waitForDeliveredSet(env, setHash);
        BEAST_EXPECT(delivered != nullptr);
        if (delivered)
            BEAST_EXPECT(delivered->getHash() == chain.rootHash);
    }

    /**
     * A root that does not hash to the set we asked for is a plain mismatch,
     * and has to leave the acquisition able to try another peer.
     *
     * @param env The environment to run in.
     */
    void
    testBadRootKeepsAcquireAlive(jtx::Env& env)
    {
        testcase("A mismatched root leaves the acquire recoverable");

        DeepChain const chain{nextSeed()};

        // Acquire an unrelated hash, so the chain's root cannot match it.
        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), uint256{42}, std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        auto const result = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());

        // A mismatched root says nothing about the tree behind the hash we asked for, so the map
        // is untouched.
        BEAST_EXPECT(acquire->isMapValid());

        // Still alive: the next packet is examined rather than waved through.
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * A second reply carrying a root we already have must stay free.
     *
     * This is what an honest second responder to the initial fan-out sends:
     * trigger() broadcasts to every tracked peer, so several answer the same
     * request and all but the first carry nothing new. Charging for that would
     * penalize peers for answering.
     *
     * Covers the root specifically, which takeNodes() short-circuits on
     * haveRoot_ without consulting the map. A repeated non-root node takes the
     * other route, through addKnownNode() - see
     * testDuplicateNonRootReplyIsFree().
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateRootReplyIsFree(jtx::Env& env)
    {
        testcase("A reply of a root we already have is free");

        DeepChain const chain{nextSeed()};
        auto& inbound = env.app().getInboundTransactions();

        // getSet() with acquire=true registers the TransactionAcquire that gotData() then looks up
        // by hash.
        uint256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        auto const rootPacket = packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}});

        // The first responder supplies the root, which is genuinely useful.
        auto const firstPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, firstPeer, rootPacket);
        BEAST_EXPECT(firstPeer->charges().empty());

        // The second sends the same root. Nothing is added to the map, but the peer did what we
        // asked, so it must not be charged.
        auto const secondPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, secondPeer, rootPacket);
        BEAST_EXPECT(secondPeer->charges().empty());
    }

    /**
     * A repeated non-root node must stay free too.
     *
     * The counterpart to testDuplicateRootReplyIsFree(), covering the
     * route that does consult the map: addKnownNode() reports a node it
     * already holds as a duplicate, and takeNodes() tests isGood(), which
     * counts a duplicate as success. Testing isUseful() there instead
     * would turn every honest second responder into a peer we charge for
     * invalid data.
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateNonRootReplyIsFree(jtx::Env& env)
    {
        testcase("A repeated non-root node is free");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        uint256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        // Depth 1 alone, so the set stays incomplete and the acquisition keeps examining data
        // rather than waving the second copy through as a late reply.
        auto const level1 = packetFor(chain, {{chain.idAt(1), chain.nodeAt(1)}});

        auto const firstPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, firstPeer, level1);
        BEAST_EXPECT(firstPeer->charges().empty());

        auto const secondPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, secondPeer, level1);
        BEAST_EXPECT(secondPeer->charges().empty());
    }

    /**
     * A reply whose node data cannot be deserialized is charged for.
     *
     * gotData() rejects the packet before the acquisition is handed
     * anything, so this pins the charge on the dispatch layer rather than
     * on takeNodes(). It also gives this suite's "was not charged"
     * assertions their teeth: a harness that recorded no charge at all
     * would satisfy all of them and fail only here.
     *
     * @param env The environment to run in.
     */
    void
    testUndeserializableNodeIsCharged(jtx::Env& env)
    {
        testcase("A reply with undeserializable node data is charged");

        DeepChain const chain{nextSeed()};
        auto& inbound = env.app().getInboundTransactions();

        uint256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // A single byte naming a wire type that does not exist, so getTreeNode() rejects it
        // before the acquisition is handed anything.
        auto packet = std::make_shared<protocol::TMLedgerData>();
        packet->set_ledgerhash(setHash.data(), uint256::size());
        packet->set_ledgerseq(0);
        packet->set_type(protocol::liTS_CANDIDATE);

        auto* const node = packet->add_nodes();
        node->set_nodedata("\xff", 1);
        node->set_id(SHAMapNodeID{}.getRawString());

        auto const peer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peer, packet);

        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});
    }

    /**
     * init() asks only the peers that claim to have the set.
     *
     * addPeers() passes hasTxSet(hash_) as its filter and trigger() as its
     * callback, so a peer that says it has the set is asked and one that says
     * it does not is left alone. Getting this wrong wastes a request on every
     * peer in the overlay for every set.
     *
     * @param env The environment to run in.
     */
    void
    testInitAsksOnlyPeersWithTheSet(jtx::Env& env)
    {
        testcase("init() asks only the peers that have the set");

        DeepChain const chain{nextSeed()};

        // Ordered with the useless peer first, so a filter that is ignored altogether
        // shows up as the wrong peer being asked rather than as one extra request.
        auto const withoutSet = std::make_shared<ChargeRecordingPeer>(false);
        auto const withSet = std::make_shared<ChargeRecordingPeer>(true);

        auto peerSet = std::make_unique<RequestCountingPeerSet>(
            std::vector<std::shared_ptr<Peer>>{withoutSet, withSet});
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet));

        static constexpr int kStartPeers = 2;
        acquire->init(kStartPeers);

        // Stop the retry loop, which would otherwise keep offering the same candidates
        // for as long as this case runs.
        acquire->cancel();

        BEAST_EXPECT(peerSetPtr->firstLimit() == kStartPeers);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::id_t>{withSet->id()});

        // The peer that was added is also asked, rather than merely tracked.
        BEAST_EXPECT(peerSetPtr->requests() >= 1);
    }

    /**
     * A timed-out acquisition asks again, and examines data again, once
     * stillNeed() revives it.
     *
     * The pending timer is private to TimeoutCounter, so this drives it
     * through a real timeout chain rather than cancel(): cancel() alone
     * never arms a timer, so reviving it afterwards would restart a chain
     * that was never running in the first place, proving nothing about
     * restarting one that timed out.
     *
     * @param env The environment to run in.
     */
    void
    testRevivedAcquireCanRequestAgain(jtx::Env& env)
    {
        testcase("A revived acquire asks again and accepts data again");

        DeepChain const chain{nextSeed()};

        // One candidate, offered once by init(). RequestCountingPeerSet dedups by tracked id like
        // the real peer set, so onTimer()'s later addPeers(1) calls never re-offer it; any further
        // request to it has to come from onTimer()'s broadcast trigger(nullptr) instead.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        // A short interval, since this case waits out a whole timeout chain.
        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > 0; }));
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::id_t>{candidate->id()});

        // A root that cannot hash to this acquisition's set, so polling with it never sets
        // haveRoot_ and so cannot itself mask the acquisition failing on its own. See
        // testTimerRetriesThenGivesUp, which probes the same way.
        DeepChain const wrongChain{nextSeed()};
        auto const probe = [&] {
            return wasIgnored(acquire->takeNodes(
                {{SHAMapNodeID{}, wrongChain.nodeAt(0)}}, std::make_shared<ChargeRecordingPeer>()));
        };

        // Nothing here ever reports progress, so onTimer() counts a timeout every tick; past
        // kMaxTimeouts (20) it fails itself and stops examining data.
        BEAST_EXPECT(waitFor(probe));
        int const requestsBeforeRevival = peerSetPtr->requests();

        // Revived, so the timer chain restarts. stillNeed() clamps timeouts_ down to
        // kNormTimeouts rather than to zero, so the very next tick broadcasts to every peer
        // already tracked - the only way a peer already selected once is asked again, and so
        // what shows the timer chain was restarted rather than just the failed flag cleared.
        acquire->stillNeed();
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > requestsBeforeRevival; }));

        // Data is examined again too: the real root is accepted, and asks for the next level.
        auto const peer = std::make_shared<ChargeRecordingPeer>();
        auto const revived = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(!wasIgnored(revived));
        BEAST_EXPECT(revived.isUseful());

        // Stop the retry loop, which would otherwise keep asking for as long as this case runs.
        acquire->cancel();
    }

    /**
     * A running acquisition keeps the wait it already has.
     *
     * The other half of the same guard: consensus asks for a set it
     * still needs once per round, so without the early return every ask
     * would re-arm the timer and a set asked for more often than the
     * interval would never tick at all. setTimer() cancels any pending
     * wait, so calling stillNeed() faster than the interval is what
     * makes that visible.
     *
     * Keeps the production interval, unlike the case above: the asking
     * has to be clearly faster than the wait for a surviving tick to
     * mean anything.
     *
     * @param env The environment to run in.
     */
    void
    testStillNeedLeavesARunningAcquireAlone(jtx::Env& env)
    {
        testcase("A running acquire keeps the wait it has");

        // One candidate, so every tick that survives produces a request.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        // An unrelated hash: nothing here feeds it data, so it stays incomplete and keeps asking.
        auto const acquire =
            std::make_shared<TransactionAcquire>(env.app(), uint256{43}, std::move(peerSet));

        // init() asks the candidate once and arms the timer. That first request is not the one
        // under test, so count from here.
        acquire->init(1);
        int const requestsFromInit = peerSetPtr->requests();

        // Ask again far faster than the interval, the way a short consensus round would. Every ask
        // clamps the timeout count, so the acquisition cannot give up while this runs.
        auto const askAgainRepeatedly = [&] {
            acquire->stillNeed();
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            return peerSetPtr->requests() > requestsFromInit;
        };

        // A tick gets through despite the asking, which it could not if each ask re-armed the wait.
        BEAST_EXPECT(waitFor(askAgainRepeatedly));

        acquire->cancel();
    }

    /**
     * The retry timer re-asks with no peer of its own, then gives up on
     * its own.
     *
     * Pins the two behaviors, not the thresholds they trip at: bounding those
     * means asserting on wall clock. Both are read in one poll, so a fast
     * interval cannot let the give-up land between the two readings.
     *
     * @param env The environment to run in.
     */
    void
    testTimerRetriesThenGivesUp(jtx::Env& env)
    {
        testcase("The retry timer re-asks, then gives up");

        DeepChain const chain{nextSeed()};

        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        // An unrelated hash, so the probe below can never be accepted. See probe().
        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), uint256{42}, std::move(peerSet), kFastRetry);

        // No candidates, so nothing goes out until onTimer() decides to broadcast.
        acquire->init(1);
        BEAST_EXPECT(peerSetPtr->requests() == 0);

        // A root that cannot hash to this acquisition's set is rejected without recording
        // progress, so polling with it does not postpone the timeout being waited for. A fresh
        // peer each time keeps the rejections from piling up on one.
        auto const probe = [&] {
            return wasIgnored(acquire->takeNodes(
                {{SHAMapNodeID{}, chain.nodeAt(0)}}, std::make_shared<ChargeRecordingPeer>()));
        };

        // kNormTimeouts (4) intervals in, onTimer() starts asking again with no peer of its own to
        // ask, and the acquisition is still examining data at that point.
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > 0 && !probe(); }));

        // Past kMaxTimeouts (20) it fails itself, and stops examining data.
        BEAST_EXPECT(waitFor(probe));

        // Whichever poll saw the retry, the count it left behind is what records that it happened.
        BEAST_EXPECT(peerSetPtr->requests() > 0);
    }

    void
    run() override
    {
        // One Env for the suite, since building one costs far more than any case here. Safe
        // because every chain is seeded through nextSeed(): gotNode() puts every node it accepts
        // into the application-wide NodeCache, so cases sharing an Env must not share a hash.
        jtx::Env env{*this};

        testHappyPathCompletesAcquisition(env);
        testTwoPeersEachSupplyPartOfTheSet(env);
        testBadRootKeepsAcquireAlive(env);
        testDuplicateRootReplyIsFree(env);
        testDuplicateNonRootReplyIsFree(env);
        testUndeserializableNodeIsCharged(env);
        testInitAsksOnlyPeersWithTheSet(env);
        testStillNeedLeavesARunningAcquireAlone(env);

        // Last: both wait out a whole timeout chain, and neither needs the cases above to have
        // run first.
        testRevivedAcquireCanRequestAgain(env);
        testTimerRetriesThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(TransactionAcquire, app, xrpl);

}  // namespace xrpl::test
