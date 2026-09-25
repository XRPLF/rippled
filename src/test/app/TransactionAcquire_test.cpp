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
     * TimeoutCounter::complete_ and failed_ are both protected, so this
     * stands in for either. A done acquisition returns a bare duplicate,
     * which is also what "we already have this root" reports, so the
     * cases below pair this with a request count to tell the two apart.
     *
     * @param san The verdict a takeNodes() call returned.
     * @return Whether that verdict shows the data was never looked at.
     */
    static bool
    wasIgnored(SHAMapAddNode const& san)
    {
        return tallyIs(san, 0, 0, 1);
    }

    /**
     * Whether takeNodes() declined the data and held the sender responsible.
     *
     * The counterpart to wasIgnored(): a set whose map cannot be
     * satisfied rejects further replies outright rather than reporting
     * them as merely unwanted. Both cost the sender the same, so this is
     * about the verdict rather than the fee.
     *
     * @param san The verdict a takeNodes() call returned.
     * @return Whether that verdict shows the sender was held responsible.
     */
    static bool
    wasRejected(SHAMapAddNode const& san)
    {
        return tallyIs(san, 0, 1, 0);
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
     * A chain reaching kLeafDepth must end the acquisition outright.
     *
     * @param env The environment to run in.
     */
    void
    testFabricatedChainFailsAcquire(jtx::Env& env)
    {
        testcase("A chain reaching kLeafDepth fails the acquire");

        DeepChain const chain{nextSeed()};

        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet));
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        // The root hashes to the set we asked for, so it is accepted.
        auto const rootResult = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(rootResult, 1, 0, 0), rootResult.get());
        BEAST_EXPECT(acquire->isMapValid());

        // Accepting the root asks the peer for more, which is what the failure below has to stop.
        int const requestsWhileAlive = peerSetPtr->requests();
        BEAST_EXPECT(requestsWhileAlive > 0);

        // Now the rest of the chain, ending in the inner node at kLeafDepth that no valid tree
        // can hold.
        auto const result = acquire->takeNodes(chain.nodesBelowRoot(), peer);

        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());
        BEAST_EXPECT(!acquire->isMapValid());

        // The acquisition is now dead: further data is not examined and no further requests go
        // out, so the failure is recorded where the map is found invalid rather than left to a
        // later trigger(). Rejected rather than merely ignored, since a later reply for a hash with
        // no valid tree is worthless to everyone.
        auto const afterFailure = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(wasRejected(afterFailure));
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileAlive);

        // stillNeed() revives a timed-out acquire, but must not revive this one: no valid tree
        // exists for the hash, so re-arming it would only re-fail on the next packet. It says so,
        // which is what stops InboundTransactions holding the entry out of newRound()'s reach.
        BEAST_EXPECT(!acquire->stillNeed());
        BEAST_EXPECT(wasRejected(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileAlive);
    }

    /**
     * A node that is merely wrong must leave the acquisition alive, so another
     * peer can still complete it.
     *
     * @param env The environment to run in.
     */
    void
    testWrongNodeKeepsAcquireAlive(jtx::Env& env)
    {
        testcase("A merely-wrong node leaves the acquire recoverable");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        auto const rootResult = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(rootResult, 1, 0, 0), rootResult.get());

        // nodeAt(1) is exactly the node the root is missing and its hash matches, but we label it
        // as living at depth 2. It cannot be hooked anywhere, yet the map stays sound.
        auto const result =
            acquire->takeNodes({{SHAMapNodeID{2, uint256{}}, chain.nodeAt(1)}}, peer);

        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());
        BEAST_EXPECT(acquire->isMapValid());

        // Still alive: the next packet is examined rather than ignored.
        BEAST_EXPECT(
            !wasIgnored(acquire->takeNodes({{SHAMapNodeID{2, uint256{}}, chain.nodeAt(1)}}, peer)));
    }

    /**
     * A root that does not hash to the set we asked for is a plain mismatch,
     * not a structural impossibility.
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

        // Charged at the recoverable tier, not the fabrication one: a mismatched root proves
        // nothing about the tree behind the hash we asked for.
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // Still alive: the next packet is examined rather than waved through.
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * A reply carrying no nodes at all is charged for.
     *
     * PeerImp rejects an empty node list before dispatch, so this is
     * defensive, but it is still the one rejection whose charge is
     * decided before any data is looked at. An empty reply says nothing
     * about the map, so the acquisition stays alive.
     *
     * @param env The environment to run in.
     */
    void
    testEmptyReplyIsCharged(jtx::Env& env)
    {
        testcase("A reply carrying no nodes is charged as invalid data");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        auto const result = acquire->takeNodes({}, peer);

        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // Nothing was touched, so another peer can still complete the set.
        BEAST_EXPECT(acquire->isMapValid());
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * The fee tier split, driven through InboundTransactions::gotData().
     *
     * Goes through the real dispatch rather than reproducing its branch, so
     * charging the wrong tier - or dropping the distinction - fails here.
     *
     * @param env The environment to run in.
     */
    void
    testFeeTierDistinguishesFabrication(jtx::Env& env)
    {
        testcase("Map-invalidating data is charged more harshly than wrong data");

        // Guard the premise: if the tiers were equal, the distinction would be cosmetic.
        BEAST_EXPECT(resource::kFeeMalformedData.cost() > resource::kFeeInvalidData.cost());

        DeepChain const chain{nextSeed()};
        auto& inbound = env.app().getInboundTransactions();

        // getSet() with acquire=true registers the TransactionAcquire that gotData() looks up.
        uint256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // Feed the root first, so the acquire has somewhere to hook the rest.
        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        // A real node labeled with the wrong position: wrong, but the map stays sound, so this is
        // the generic tier.
        auto const wrongPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(
            setHash, wrongPeer, packetFor(chain, {{SHAMapNodeID{2, uint256{}}, chain.nodeAt(1)}}));
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});

        // The chain, ending in an inner node at kLeafDepth. This invalidates the map, so it costs
        // the harsher tier.
        auto const fabricatingPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, fabricatingPeer, packetFor(chain, chain.nodesBelowRoot()));
        BEAST_EXPECT(fabricatingPeer->charges() == std::vector{resource::kFeeMalformedData});

        // Data arriving after the set is over costs the useless tier, not the harsher one: the
        // sender of this packet is not the one that broke the set. rootPeer's accepted root
        // earned the one targeted follow-up request that is this test's only source of an
        // allowance to spend - there are no other real peers here. Its own late reply is free;
        // a second one from it has already spent that pass and is a replay. See
        // testLateReplyIsFreeOncePerPeerAsked() for the peer-identity cases (a stranger nobody
        // asked, and a second reply from the same peer) in isolation.
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges() == std::vector{resource::kFeeUselessData});
    }

    /**
     * A second reply carrying a root we already have must stay free.
     *
     * This is what an honest second responder to the initial fan-out
     * sends: trigger() broadcasts to every tracked peer, so several
     * answer the same request and all but the first carry nothing new.
     * takeNodes() therefore tests isGood() rather than isUseful(): an
     * all-duplicate batch is good but not useful, and charging it would
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
     * A reply arriving after the set is settled is free once for the peer
     * we asked, and charged after that.
     *
     * trigger() sends to every peer it was given, so when one of them
     * settles the set the others' replies are already in flight and none
     * of those senders could have known - including when what settled
     * the set was a failure another peer caused. Charging them taxes
     * honest peers for someone else's doing. The pass belongs to the
     * specific peer requestedPeers_ says was asked, not to whichever late
     * reply happens to arrive first: a peer nobody asked gets no benefit
     * from it, and a peer that already spent it is replaying. What must
     * not be free at all is a replay: InboundTransactions::gotData()
     * deserializes and hashes the whole node list before takeNodes() ever
     * sees it, so an unbounded number of them would otherwise cost a
     * sender nothing but the per-message fee.
     *
     * @param env The environment to run in.
     */
    void
    testLateReplyIsFreeOncePerPeerAsked(jtx::Env& env)
    {
        testcase("A late reply is free once per peer we asked");

        // A chain ending in a leaf, so the set settles rather than failing.
        auto const chain = DeepChain::toLeaf(1, nextSeed());

        // One peer asked, so it is the only one whose late reply can legitimately be free.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::id_t>{candidate->id()});

        // The whole chain in one batch, from a different peer, which settles the set.
        auto data = chain.nodesBelowRoot();
        data.emplace(data.begin(), SHAMapNodeID{}, chain.nodeAt(0));

        auto const supplier = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes(std::move(data), supplier).isUseful());
        BEAST_EXPECT(supplier->charges().empty());

        // A peer nobody asked is charged immediately: the allowance belongs to candidate, not
        // to whichever late reply happens to arrive first.
        auto const stranger = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges() == std::vector{resource::kFeeUselessData});

        // candidate's own late reply is the one that was genuinely in flight, and is free.
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges().empty());

        // A second reply from candidate has already spent its pass: this one is a replay.
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges() == std::vector{resource::kFeeUselessData});

        acquire->cancel();
    }

    /**
     * One peer replaying its own late reply must not spend a different,
     * genuinely honest peer's allowance.
     *
     * The allowance is tracked by which specific peers in requestedPeers_
     * have redeemed it (lateReplyGranted_), not by a shared count compared
     * against requestedPeers_.size(). A count cannot tell whose slot a
     * reply is spending, so one peer resending its own already-accepted
     * reply several times would exhaust the whole allowance and leave a
     * second, honestly-asked peer's first and only late reply charged for
     * someone else's replaying.
     *
     * @param env The environment to run in.
     */
    void
    testOnePeersReplaysDoNotStarveAnother(jtx::Env& env)
    {
        testcase("One peer's replays do not spend a different peer's allowance");

        auto const chain = DeepChain::toLeaf(1, nextSeed());

        // Two peers asked, so each earns its own pass.
        auto const spammer = std::make_shared<ChargeRecordingPeer>();
        auto const honest = std::make_shared<ChargeRecordingPeer>();
        auto peerSet = std::make_unique<RequestCountingPeerSet>(
            std::vector<std::shared_ptr<Peer>>{spammer, honest});

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(2);

        // A third peer supplies the whole chain, so both spammer's and honest's replies below
        // are late.
        auto data = chain.nodesBelowRoot();
        data.emplace(data.begin(), SHAMapNodeID{}, chain.nodeAt(0));
        auto const supplier = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes(std::move(data), supplier).isUseful());

        // spammer's first late reply is free - its own pass - but every one after that is its
        // own replay, not anyone else's slot to spend.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, spammer)));
        BEAST_EXPECT(spammer->charges().empty());
        for (int i = 0; i < 5; ++i)
            acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, spammer);
        BEAST_EXPECT(spammer->charges().size() == 5);

        // honest's own, single late reply is still free. A shared count would have let
        // spammer's five replays above exhaust the allowance before honest ever got here.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, honest)));
        BEAST_EXPECT(honest->charges().empty());

        acquire->cancel();
    }

    /**
     * A revived acquisition's late-reply allowance starts over, rather than
     * carrying over the round that just failed.
     *
     * The allowance is one pass per peer asked, for the round that settled -
     * that peer's reply can still be in flight when it does. stillNeed()
     * reviving a timed-out acquisition starts a fresh round; a peer that
     * already spent its pass in the round before must not have that carry
     * over and mischarge the fresh pass the new round owes it. Both rounds
     * here are settled with cancel() alone, so no real data has to flow to
     * demonstrate it: only lateReplyGranted_'s behavior across the revival is
     * under test.
     *
     * @param env The environment to run in.
     */
    void
    testLateReplyAllowanceResetsOnRevival(jtx::Env& env)
    {
        testcase("A revived acquisition's late-reply allowance is not carried over");

        DeepChain const chain{nextSeed()};

        // One peer asked, so it is the one whose pass is under test in both rounds.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);

        // The first round fails, and candidate's one allowed late reply arrives and is free,
        // spending its pass for this round.
        acquire->cancel();
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges().empty());

        // Revived: the map is still valid, so stillNeed() clears the failure, restarts the
        // timer, and hands out a fresh pass for the round that follows.
        BEAST_EXPECT(acquire->stillNeed());

        // The second round fails too - cancel() alone settles it, so no data has to flow.
        acquire->cancel();

        // candidate's late reply here is the second round's own pass, not a reuse of the first
        // round's already-spent one. Without clearing lateReplyGranted_ on revival, this would
        // be charged as a repeat instead.
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges().empty());

        acquire->cancel();
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
     * Each peer is charged for its own packet, not for whatever the map happens
     * to look like afterwards.
     *
     * takeNodes() classifies and charges under one lock hold. Reading the
     * tier back after it returns would let a second packet arriving in
     * between - up to five run concurrently under JtTxnData - invalidate
     * the map and make the first peer pay the fabrication tier for data
     * it did not send.
     *
     * @param env The environment to run in.
     */
    void
    testChargeIsNotDecidedAfterTheLock(jtx::Env& env)
    {
        testcase("A peer is charged for its own packet only");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());

        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, rootPeer);
        BEAST_EXPECT(rootPeer->charges().empty());

        // A misplaced node: wrong, but the map survives, so the generic tier.
        auto const wrongPeer = std::make_shared<ChargeRecordingPeer>();
        acquire->takeNodes({{SHAMapNodeID{2, uint256{}}, chain.nodeAt(1)}}, wrongPeer);
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});

        // Now a second peer invalidates the map, which must leave the earlier verdicts alone.
        auto const fabricatingPeer = std::make_shared<ChargeRecordingPeer>();
        acquire->takeNodes(chain.nodesBelowRoot(), fabricatingPeer);
        BEAST_EXPECT(fabricatingPeer->charges() == std::vector{resource::kFeeMalformedData});
        BEAST_EXPECT(!acquire->isMapValid());

        // The earlier peers' charges must be untouched by that.
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});
        BEAST_EXPECT(rootPeer->charges().empty());
    }

    /**
     * A batch that ends on a bad node still counts the good nodes ahead of it,
     * and a batch that achieved nothing records no progress.
     *
     * The recorded progress is what the verdict is for: it stops the
     * next timer tick from counting a timeout, so reporting only the
     * node the batch stopped on would push an acquisition that is
     * genuinely advancing toward kMaxTimeouts, while recording progress
     * for a batch we already had would hold a stalled one open. The flag
     * is read rather than the returned tally, which only stands in for
     * it, and cleared between batches so each reading is about the batch
     * just fed.
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
        acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(acquire->madeProgress());
        acquire->clearProgress();

        // Good nodes at depths 1 and 2, then a further chain node mislabeled at a position it
        // cannot occupy. The map stays sound, so only the last node is bad.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> batch;
        batch.emplace_back(SHAMapNodeID{1, uint256{}}, chain.nodeAt(1));
        batch.emplace_back(SHAMapNodeID{2, uint256{}}, chain.nodeAt(2));
        batch.emplace_back(SHAMapNodeID{9, uint256{}}, chain.nodeAt(3));

        auto const san = acquire->takeNodes(batch, peer);

        // The verdict names both halves rather than just the failure, and the two good nodes are
        // what the next timer tick must not count as a timeout. The batch stops on the bad node,
        // so exactly one is counted however many were left unexamined behind it.
        BEAST_EXPECTS(tallyIs(san, 2, 1, 0), san.get());
        BEAST_EXPECT(san.isUseful());
        BEAST_EXPECT(acquire->madeProgress());
        BEAST_EXPECT(acquire->isMapValid());

        // The bad node is still charged for, at the recoverable tier.
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // A batch of nothing but the root we already have: counted as a duplicate rather than
        // reaching the clean exit with nothing counted, so it is neither reported as useful nor
        // allowed to postpone the timeout.
        acquire->clearProgress();
        auto const repeatedRoot = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);

        BEAST_EXPECTS(tallyIs(repeatedRoot, 0, 0, 1), repeatedRoot.get());
        BEAST_EXPECT(repeatedRoot.isGood());
        BEAST_EXPECT(!repeatedRoot.isUseful());
        BEAST_EXPECT(!acquire->madeProgress());

        // The same root alongside a node we do need: the duplicate is reported as one, and the
        // node that did hook in is what records the progress.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> mixed;
        mixed.emplace_back(SHAMapNodeID{}, chain.nodeAt(0));
        mixed.emplace_back(SHAMapNodeID{3, uint256{}}, chain.nodeAt(3));

        auto const withDuplicateRoot = acquire->takeNodes(mixed, peer);

        BEAST_EXPECTS(tallyIs(withDuplicateRoot, 1, 0, 1), withDuplicateRoot.get());
        BEAST_EXPECT(withDuplicateRoot.isUseful());
        BEAST_EXPECT(acquire->madeProgress());

        // None of that cost the sender anything beyond the one bad node above.
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
        BEAST_EXPECT(acquire->stillNeed());
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > requestsBeforeRevival; }));

        // Data is examined again too: the real root is accepted and asks for the next level, and
        // the sender is not charged for data we are asking for once more.
        auto const peer = std::make_shared<ChargeRecordingPeer>();
        auto const revived = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(!wasIgnored(revived));
        BEAST_EXPECT(revived.isUseful());
        BEAST_EXPECT(peer->charges().empty());

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
            static_cast<void>(acquire->stillNeed());
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
        testFabricatedChainFailsAcquire(env);
        testWrongNodeKeepsAcquireAlive(env);
        testBadRootKeepsAcquireAlive(env);
        testEmptyReplyIsCharged(env);
        testFeeTierDistinguishesFabrication(env);
        testDuplicateRootReplyIsFree(env);
        testDuplicateNonRootReplyIsFree(env);
        testLateReplyIsFreeOncePerPeerAsked(env);
        testOnePeersReplaysDoNotStarveAnother(env);
        testLateReplyAllowanceResetsOnRevival(env);
        testUndeserializableNodeIsCharged(env);
        testChargeIsNotDecidedAfterTheLock(env);
        testPartialBatchIsCounted(env);
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
