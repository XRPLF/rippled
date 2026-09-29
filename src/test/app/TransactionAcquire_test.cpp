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
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <xrpl.pb.h>

#include <chrono>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * An acquisition that exposes state its bases keep protected.
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
     * InboundTransactions keys its acquisitions by set hash. A fresh seed
     * per chain gives every chain distinct hashes, so each case resolves
     * and revives only its own.
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
     * TimeoutCounter::complete_ and failed_ are both protected, so this
     * stands in for either: a done acquisition returns a verdict with every
     * count at zero.
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
     * done() hands the map over through a job, so a delivered set is what
     * separates completion from a stop.
     *
     * @param env The environment whose InboundTransactions to watch.
     * @param setHash The set to wait for.
     * @return The delivered map, or nullptr if none arrived.
     */
    [[nodiscard]] static std::shared_ptr<SHAMap>
    waitForDeliveredSet(jtx::Env& env, UInt256 const& setHash)
    {
        auto& inbound = env.app().getInboundTransactions();

        // acquire=false keeps this to a lookup rather than registering an acquisition. The job is
        // queued before takeNodes() returns, so a few seconds is a generous deadline.
        std::shared_ptr<SHAMap> delivered;
        if (!waitFor(
                [&] { return (delivered = inbound.getSet(setHash, false)) != nullptr; },
                std::chrono::seconds{5}))
            return nullptr;
        return delivered;
    }

    /**
     * A chain ending in a leaf completes the acquisition, which stops the
     * asking and hands the map to InboundTransactions. Later replies for it
     * are then left alone.
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

        UInt256 const setHash = chain.rootHash.asUInt256();
        auto const acquire =
            std::make_shared<TransactionAcquire>(env.app(), setHash, std::move(peerSet));
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        // The root alone leaves the set incomplete, so accepting it asks for the level below.
        auto const rootResult = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(rootResult.isUseful());
        int const requestsWhileIncomplete = peerSetPtr->requests();
        BEAST_EXPECT(requestsWhileIncomplete > 0);

        // The rest of the chain, ending in the leaf.
        auto const result = acquire->takeNodes(chain.nodesBelowRoot(), peer);
        BEAST_EXPECT(result.isUseful());
        BEAST_EXPECT(!result.isInvalid());

        // The request count holds steady, so the asking has stopped.
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileIncomplete);

        auto const delivered = waitForDeliveredSet(env, setHash);
        BEAST_EXPECT(delivered != nullptr);
        if (delivered)
        {
            BEAST_EXPECT(delivered->getHash() == chain.rootHash);
            BEAST_EXPECT(delivered->isValid());
        }

        // A reply arriving after the set is finished is left alone, and the asking stays stopped.
        // init() stayed uncalled, so the set above is what settled the acquisition.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileIncomplete);
    }

    /**
     * Two peers each answering with a different missing piece are both accepted
     * without penalty, and the set completes from their combined replies.
     *
     * Driven through InboundTransactions::gotData(), so the leaf goes over the
     * real dispatch, which derives a leaf's position from its own key.
     *
     * @param env The environment to run in.
     */
    void
    testTwoPeersEachSupplyPartOfTheSet(jtx::Env& env)
    {
        testcase("Two peers each supplying part of a set are both accepted without penalty");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        // acquire=true registers the TransactionAcquire that gotData() looks up by hash.
        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // The first peer answers with the root only.
        auto const peerA = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peerA, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(peerA->charges().empty());

        // The second answers with everything the first left out, and finishes the set.
        auto const peerB = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peerB, packetFor(chain, chain.nodesBelowRoot()));
        BEAST_EXPECT(peerB->charges().empty());

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
            env.app(), UInt256{42}, std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        auto const result = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());

        // A mismatched root tells the acquisition only that this peer's answer is wrong, so the
        // map keeps its state.
        BEAST_EXPECT(acquire->isMapValid());

        // Still alive: the next packet is examined rather than waved through.
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * A second reply carrying a root we already have must stay free.
     *
     * This is what an honest second responder to the initial fan-out sends:
     * trigger() broadcasts to every tracked peer, so several answer the same
     * request and all but the first carry data the map already holds, so they
     * go uncharged.
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

        // acquire=true registers the TransactionAcquire that gotData() looks up by hash.
        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        auto const rootPacket = packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}});

        // The first responder supplies the root, which is genuinely useful.
        auto const firstPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, firstPeer, rootPacket);
        BEAST_EXPECT(firstPeer->charges().empty());

        // The second sends the same root. It answered what we asked, so it goes uncharged.
        auto const secondPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, secondPeer, rootPacket);
        BEAST_EXPECT(secondPeer->charges().empty());
    }

    /**
     * A repeated non-root node stays free too.
     *
     * The counterpart to testDuplicateRootReplyIsFree(), covering the route
     * that consults the map: addKnownNode() reports a node it already holds as
     * a duplicate, which isGood() counts as success.
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateNonRootReplyIsFree(jtx::Env& env)
    {
        testcase("A repeated non-root node is free");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        UInt256 const setHash = chain.rootHash.asUInt256();
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

        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // getTreeNode() rejects this before the acquisition is handed anything.
        auto packet = std::make_shared<protocol::TMLedgerData>();
        packet->set_ledgerhash(setHash.data(), UInt256::size());
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
     * A batch that ends on a bad node still counts the good nodes ahead of
     * it, and a batch of nodes already held still counts as an answer.
     *
     * The recorded progress is what the verdict is for: it stops the
     * next timer tick from counting a timeout, so a batch that hooked
     * any node reports progress. A batch of nodes already held counts as
     * well, because the peer answered what it was asked, and that answer
     * earns the same postponement.
     * The flag is read rather than the returned tally, which only stands
     * in for it, and cleared between batches so each reading is about the
     * batch just fed.
     *
     * @param env The environment to run in.
     */
    void
    testPartialBatchIsCounted(jtx::Env& env)
    {
        testcase("A batch ending on a bad node still counts the good nodes");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        // The root is useful, so it records progress.
        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer));
        BEAST_EXPECT(acquire->madeProgress());
        acquire->clearProgress();

        // Good nodes at depths 1 and 2, then a further chain node mislabeled at a position only a
        // leaf may occupy. The map stays sound, so only the last node is bad.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> batch;
        batch.emplace_back(chain.idAt(1), chain.nodeAt(1));
        batch.emplace_back(chain.idAt(2), chain.nodeAt(2));
        batch.emplace_back(SHAMapNodeID{9, UInt256{}}, chain.nodeAt(3));

        auto const san = acquire->takeNodes(batch, peer);

        // The verdict names both halves. The batch stops on the bad node, so one bad node is
        // counted however many were left unexamined behind it.
        BEAST_EXPECTS(tallyIs(san, 2, 1, 0), san.get());
        BEAST_EXPECT(san.isUseful());
        BEAST_EXPECT(acquire->madeProgress());
        BEAST_EXPECT(acquire->isMapValid());

        // A batch holding only the root we already have: counted as a duplicate. The duplicate
        // tally is what makes it an answer, so it postpones the timeout.
        acquire->clearProgress();
        auto const repeatedRoot = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);

        BEAST_EXPECTS(tallyIs(repeatedRoot, 0, 0, 1), repeatedRoot.get());
        BEAST_EXPECT(repeatedRoot.isGood());
        BEAST_EXPECT(!repeatedRoot.isUseful());
        BEAST_EXPECT(acquire->madeProgress());

        // The same root alongside a node we do need: the duplicate is reported as one, and the
        // node that did hook in is what records the progress.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> mixed;
        mixed.emplace_back(SHAMapNodeID{}, chain.nodeAt(0));
        mixed.emplace_back(SHAMapNodeID{3, UInt256{}}, chain.nodeAt(3));

        auto const withDuplicateRoot = acquire->takeNodes(mixed, peer);

        BEAST_EXPECTS(tallyIs(withDuplicateRoot, 1, 0, 1), withDuplicateRoot.get());
        BEAST_EXPECT(withDuplicateRoot.isUseful());
        BEAST_EXPECT(acquire->madeProgress());

        // takeNodes() charges nobody: InboundTransactions::gotData() reads the verdict and decides.
        BEAST_EXPECT(peer->charges().empty());
    }

    /**
     * init() passes hasTxSet() to addPeers() as its candidate filter.
     *
     * init() hands addPeers() hasTxSet(hash_) as its hasItem callback and
     * trigger() as its onPeerAdded callback. The harness selects on that
     * callback while the real peer set only scores with it, so this pins which
     * callbacks the acquisition supplies, not how many peers production asks.
     *
     * @param env The environment to run in.
     */
    void
    testInitFiltersCandidatesByHasTxSet(jtx::Env& env)
    {
        testcase("init() passes hasTxSet as its candidate filter");

        DeepChain const chain{nextSeed()};

        // Ordered with the useless peer first, so a filter that is ignored altogether shows up as
        // the wrong peer being asked rather than as one extra request.
        auto const withoutSet = std::make_shared<ChargeRecordingPeer>(false);
        auto const withSet = std::make_shared<ChargeRecordingPeer>(true);

        auto peerSet = std::make_unique<RequestCountingPeerSet>(
            std::vector<std::shared_ptr<Peer>>{withoutSet, withSet});
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet));

        static constexpr int kStartPeers = 2;
        acquire->init(kStartPeers);

        // Stop the retry loop, which keeps offering the same candidates while the acquisition runs.
        acquire->cancel();

        BEAST_EXPECT(peerSetPtr->firstLimit() == kStartPeers);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::ID>{withSet->id()});

        // The peer that was added is also asked, rather than merely tracked.
        BEAST_EXPECT(peerSetPtr->requests() >= 1);
    }

    /**
     * A timed-out acquisition asks again, and examines data again, once
     * stillNeed() revives it.
     *
     * The pending timer is private to TimeoutCounter, so the case lets a real
     * timeout chain run rather than calling cancel().
     *
     * @param env The environment to run in.
     */
    void
    testRevivedAcquireCanRequestAgain(jtx::Env& env)
    {
        testcase("A revived acquire asks again and accepts data again");

        DeepChain const chain{nextSeed()};

        // One candidate, offered once by init(). RequestCountingPeerSet dedups by tracked id like
        // the real peer set, so a further request to it comes from onTimer()'s broadcast
        // trigger(nullptr).
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        // A short interval, since this case waits out a whole timeout chain.
        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > 0; }));
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::ID>{candidate->id()});

        // A root from a different chain, so polling with it leaves haveRoot_ false and the
        // acquisition fails on its own. See testTimerBroadcastsThenGivesUp, which probes the same
        // way.
        DeepChain const wrongChain{nextSeed()};
        auto const probe = [&] {
            return wasIgnored(acquire->takeNodes(
                {{SHAMapNodeID{}, wrongChain.nodeAt(0)}}, std::make_shared<ChargeRecordingPeer>()));
        };

        // Progress stays clear, so onTimer() counts a timeout every tick; past kMaxTimeouts (20)
        // it fails itself and stops examining data.
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

        // Stop the retry loop, which asks for as long as the acquisition runs.
        acquire->cancel();
    }

    /**
     * A running acquisition keeps the wait it already has.
     *
     * setTimer() cancels any pending wait, so calling stillNeed() faster than
     * the interval is what makes the early return observable. Keeps the
     * production interval, so the asking is clearly faster than the wait.
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

        // An unrelated hash, so the acquisition stays incomplete and keeps asking.
        auto const acquire =
            std::make_shared<TransactionAcquire>(env.app(), UInt256{43}, std::move(peerSet));

        // init() asks the candidate once and arms the timer. That first request is not the one
        // under test, so count from here.
        acquire->init(1);
        int const requestsFromInit = peerSetPtr->requests();

        // Ask again far faster than the interval, as a short consensus round does. Every ask clamps
        // the timeout count, so the acquisition keeps running throughout.
        auto const askAgainRepeatedly = [&] {
            acquire->stillNeed();
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            return peerSetPtr->requests() > requestsFromInit;
        };

        // A tick gets through despite the asking, which is what the kept wait allows.
        BEAST_EXPECT(waitFor(askAgainRepeatedly));

        acquire->cancel();
    }

    /**
     * The retry timer broadcasts with no peer of its own, then gives up on
     * its own.
     *
     * Pins the two behaviors rather than the thresholds they trip at, since
     * bounding those means asserting on wall clock. Both are read in one
     * poll, so the two readings share an instant.
     *
     * The acquisition tracks no peer, so the broadcast this case counts reaches
     * nobody in production either. What is pinned is that onTimer() issues it.
     *
     * @param env The environment to run in.
     */
    void
    testTimerBroadcastsThenGivesUp(jtx::Env& env)
    {
        testcase("The retry timer broadcasts, then gives up");

        DeepChain const chain{nextSeed()};

        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        // An unrelated hash, so the probe below is always rejected.
        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), UInt256{42}, std::move(peerSet), kFastRetry);

        // No candidates, so nothing goes out until onTimer() decides to broadcast.
        acquire->init(1);
        BEAST_EXPECT(peerSetPtr->requests() == 0);

        // A root that cannot hash to this acquisition's set is rejected without recording progress,
        // so polling with it does not postpone the timeout. A fresh peer each time keeps the
        // rejections from piling up on one.
        auto const probe = [&] {
            return wasIgnored(acquire->takeNodes(
                {{SHAMapNodeID{}, chain.nodeAt(0)}}, std::make_shared<ChargeRecordingPeer>()));
        };

        // kNormTimeouts (4) intervals in, onTimer() broadcasts with no peer of its own to ask, and
        // the acquisition is still examining data. Read as a broadcast rather than as a request,
        // since a request here would mean a peer was named.
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->broadcasts() > 0 && !probe(); }));

        // Past kMaxTimeouts (20) it fails itself, and stops examining data.
        BEAST_EXPECT(waitFor(probe));

        // The count outlives the poll that saw the broadcast.
        BEAST_EXPECT(peerSetPtr->broadcasts() > 0);
    }

    void
    run() override
    {
        // One Env for the suite, which is safe only because every chain is seeded through
        // nextSeed(): cases sharing an Env share a NodeCache, so they must not share a hash.
        jtx::Env env{*this};

        testHappyPathCompletesAcquisition(env);
        testTwoPeersEachSupplyPartOfTheSet(env);
        testBadRootKeepsAcquireAlive(env);
        testDuplicateRootReplyIsFree(env);
        testDuplicateNonRootReplyIsFree(env);
        testUndeserializableNodeIsCharged(env);
        testPartialBatchIsCounted(env);
        testInitFiltersCandidatesByHasTxSet(env);
        testStillNeedLeavesARunningAcquireAlone(env);

        // Last: both wait out a whole timeout chain, and neither needs the cases above to have
        // run first.
        testRevivedAcquireCanRequestAgain(env);
        testTimerBroadcastsThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(TransactionAcquire, app, xrpl);

}  // namespace xrpl::test
