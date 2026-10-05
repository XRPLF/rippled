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
#include <cstdint>
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
     * A root already held reports the same bare duplicate, so cases pair
     * this with a request count.
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
     * The counterpart to wasIgnored(). Both cost the sender the same, so
     * this reads the verdict rather than the fee.
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
     * The late-reply allowance survives giveSet().
     *
     * giveSet() resets the acquisition only when something else supplied the
     * set (the fromAcquire guard), so a reply after completion still reaches
     * the per-peer allowance wantsReplyFrom() judges in gotData().
     *
     * @param env The environment to run in.
     */
    void
    testLateReplyAllowanceSurvivesGiveSet(jtx::Env& env)
    {
        testcase("The late-reply allowance survives giveSet()");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // One peer supplies the whole chain, so it alone holds the allowance's one slot.
        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        inbound.gotData(setHash, rootPeer, packetFor(chain, chain.nodesBelowRoot()));
        BEAST_EXPECT(rootPeer->charges().empty());

        // The set completed and handed off through done() and giveSet().
        auto const delivered = waitForDeliveredSet(env, setHash);
        BEAST_EXPECT(delivered != nullptr);

        // giveSet() keeps the acquisition registered, so rootPeer's late reply reaches its free
        // slot in wantsReplyFrom() rather than gotData()'s no-acquisition charge.
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        // A second reply from rootPeer has already spent that slot: this one is a replay.
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges() == std::vector{resource::kFeeUselessData});
    }

    /**
     * A late reply is turned away before its nodes are parsed.
     *
     * The fee tier is what the order is visible in: the same packet charges
     * kFeeInvalidData once parsed, as testUndeserializableNodeIsCharged shows.
     *
     * @param env The environment to run in.
     */
    void
    testLateReplyIsTurnedAwayBeforeParsing(jtx::Env& env)
    {
        testcase("A late reply is turned away before its nodes are parsed");

        auto const chain = DeepChain::toLeaf(3, nextSeed());
        auto& inbound = env.app().getInboundTransactions();

        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // One peer supplies the whole chain, so it alone holds the allowance's one slot. Delivered
        // in two replies, since only a peer we asked holds a slot and it is the follow-up request
        // the root-only reply provokes that enrolls it in requestedPeers_.
        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        inbound.gotData(setHash, rootPeer, packetFor(chain, chain.nodesBelowRoot()));

        BEAST_EXPECT(waitForDeliveredSet(env, setHash) != nullptr);
        BEAST_EXPECT(rootPeer->charges().empty());

        // Node data getTreeNode() rejects, as in testUndeserializableNodeIsCharged.
        auto const garbage = [&] {
            auto packet = std::make_shared<protocol::TMLedgerData>();
            packet->set_ledgerhash(setHash.data(), UInt256::size());
            packet->set_ledgerseq(0);
            packet->set_type(protocol::liTS_CANDIDATE);

            auto* const node = packet->add_nodes();
            node->set_nodedata("\xff", 1);
            node->set_id(SHAMapNodeID{}.getRawString());
            return packet;
        };

        // Free, so it was turned away before the parse, which would have charged kFeeInvalidData.
        inbound.gotData(setHash, rootPeer, garbage());
        BEAST_EXPECT(rootPeer->charges().empty());

        // The slot is spent, so this one is charged as a replay rather than as bad data.
        inbound.gotData(setHash, rootPeer, garbage());
        BEAST_EXPECT(rootPeer->charges() == std::vector{resource::kFeeUselessData});
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

        // The rest of the chain, ending in the inner node at kLeafDepth no valid tree can hold.
        auto const result = acquire->takeNodes(chain.nodesBelowRoot(), peer);

        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());
        BEAST_EXPECT(!acquire->isMapValid());

        // The acquisition is now dead: later data is left unexamined, the request count holds, and
        // a later reply is rejected, since the verdict holds for every peer.
        auto const afterFailure = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(wasRejected(afterFailure));
        BEAST_EXPECT(peerSetPtr->requests() == requestsWhileAlive);

        // stillNeed() keeps this one failed and reports so, which lets InboundTransactions leave
        // the entry for newRound() to sweep.
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

        // nodeAt(1) is the node the root is missing, labeled as living at depth 2. It cannot be
        // hooked anywhere, and the map stays sound.
        auto const result =
            acquire->takeNodes({{SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1)}}, peer);

        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());
        BEAST_EXPECT(acquire->isMapValid());

        // Still alive: the next packet is examined rather than ignored.
        BEAST_EXPECT(
            !wasIgnored(acquire->takeNodes({{SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1)}}, peer)));
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
            env.app(), UInt256{42}, std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        auto const result = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(result, 0, 1, 0), result.get());

        // A mismatched root says only that this peer's answer is wrong, so the map keeps its state.
        BEAST_EXPECT(acquire->isMapValid());

        // The recoverable tier: a mismatched root says nothing about the tree behind the hash we
        // asked for.
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // Still alive: the next packet is examined rather than waved through.
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * A reply carrying no nodes is charged for.
     *
     * An empty reply says nothing about the map, so the acquisition stays
     * alive. PeerImp rejects an empty node list before dispatch, so this is
     * defensive.
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

        // The map is untouched, so another peer can still complete the set.
        BEAST_EXPECT(acquire->isMapValid());
        BEAST_EXPECT(!wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer)));
    }

    /**
     * The fee tier split, driven through InboundTransactions::gotData().
     *
     * Goes through the real dispatch rather than reproducing its branch.
     *
     * @param env The environment to run in.
     */
    void
    testFeeTierDistinguishesFabrication(jtx::Env& env)
    {
        testcase("Map-invalidating data is charged more harshly than wrong data");

        // Guard the premise: the two tiers carry different charges.
        BEAST_EXPECT(resource::kFeeMalformedData.cost() > resource::kFeeInvalidData.cost());

        DeepChain const chain{nextSeed()};
        auto& inbound = env.app().getInboundTransactions();

        // acquire=true registers the TransactionAcquire that gotData() looks up by hash.
        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // Feed the root first, so the acquire has somewhere to hook the rest.
        auto const rootPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        // A real node labeled with the wrong position. The map stays sound, so this is the
        // generic tier.
        auto const wrongPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(
            setHash, wrongPeer, packetFor(chain, {{SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1)}}));
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});

        // The chain, ending in an inner node at kLeafDepth. The map goes invalid, so it costs the
        // harsher tier.
        auto const fabricatingPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, fabricatingPeer, packetFor(chain, chain.nodesBelowRoot()));
        BEAST_EXPECT(fabricatingPeer->charges() == std::vector{resource::kFeeMalformedData});

        // Data arriving after the set is over costs the useless tier, not the harsher one: the
        // sender of this packet is not the one that broke the set. rootPeer's accepted root earned
        // the one allowance this case has to spend, so its own late reply is free and a second one
        // is a replay. See testLateReplyIsFreeOncePerPeerAsked() for those cases in isolation.
        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges().empty());

        inbound.gotData(setHash, rootPeer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(rootPeer->charges() == std::vector{resource::kFeeUselessData});
    }

    /**
     * A second reply carrying a root we already have stays free.
     *
     * trigger() broadcasts to every tracked peer, so all but the first
     * responder carry nothing new, which isGood() counts as success. Covers
     * the root, which short-circuits on haveRoot_. The non-root route is
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
     * A reply arriving after the set is settled is free once for the peer we
     * asked, and charged after that.
     *
     * trigger() sends to every peer it was given, so when one settles the set
     * the others' replies are already in flight. The pass belongs to the
     * specific peer requestedPeers_ says was asked, so a peer nobody asked
     * gets none of it and a second reply from the same peer is charged.
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
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::ID>{candidate->id()});

        // The whole chain in one batch, from a different peer, which settles the set.
        auto data = chain.allNodes();

        auto const supplier = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes(std::move(data), supplier).isUseful());
        BEAST_EXPECT(supplier->charges().empty());

        // A peer nobody asked is charged immediately: the allowance belongs to candidate.
        auto const stranger = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges() == std::vector{resource::kFeeUselessData});

        // candidate's own late reply was genuinely in flight, and is free.
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
     * A reply to a settled set earns no timeout postponement.
     *
     * The settled path reports a duplicate, which is what the running path
     * reads as an answer worth postponing a timeout for. That report is about
     * a round that has ended, so the postponement is read from whether the set
     * was settled when the call arrived rather than from the tally.
     *
     * @param env The environment to run in.
     */
    void
    testSettledReplyEarnsNoProgress(jtx::Env& env)
    {
        testcase("A reply to a settled set records no progress");

        // A chain ending in a leaf, so the set settles rather than failing.
        auto const chain = DeepChain::toLeaf(1, nextSeed());

        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);

        // The whole chain in one batch, which settles the set.
        auto data = chain.nodesBelowRoot();
        data.emplace(data.begin(), SHAMapNodeID{}, chain.nodeAt(0));
        BEAST_EXPECT(acquire->takeNodes(std::move(data), candidate).isUseful());

        // That batch earned its own progress, so the flag is cleared ahead of the reading
        // this case is about.
        BEAST_EXPECT(acquire->madeProgress());
        acquire->clearProgress();

        // The late reply reports the bare duplicate a replay reports, and records nothing.
        auto const late = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate);
        BEAST_EXPECTS(wasIgnored(late), late.get());
        BEAST_EXPECT(!acquire->madeProgress());

        acquire->cancel();
    }

    /**
     * A peer's repeated late replies spend only that peer's own pass.
     *
     * lateReplyGranted_ records which peers have redeemed the allowance rather
     * than a count, which could not tell whose pass a reply spends.
     *
     * @param env The environment to run in.
     */
    void
    testLateReplyPassIsKeyedByPeer(jtx::Env& env)
    {
        testcase("One peer's replays do not spend a different peer's allowance");

        auto const chain = DeepChain::toLeaf(1, nextSeed());

        // Two peers asked, so each earns its own pass.
        auto const replayer = std::make_shared<ChargeRecordingPeer>();
        auto const otherPeer = std::make_shared<ChargeRecordingPeer>();
        auto peerSet = std::make_unique<RequestCountingPeerSet>(
            std::vector<std::shared_ptr<Peer>>{replayer, otherPeer});

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(2);

        // A third peer supplies the whole chain, so both replayer's and otherPeer's replies below
        // are late.
        auto data = chain.allNodes();
        auto const supplier = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes(std::move(data), supplier).isUseful());

        // replayer's first late reply is its own pass, and every one after that is its own replay.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, replayer)));
        BEAST_EXPECT(replayer->charges().empty());
        for (int i = 0; i < 5; ++i)
            static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, replayer));
        BEAST_EXPECT(replayer->charges().size() == 5);

        // otherPeer's own late reply is still free: the replays above spent only replayer's slot.
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, otherPeer)));
        BEAST_EXPECT(otherPeer->charges().empty());

        acquire->cancel();
    }

    /**
     * A peer whose pass is spent earns a fresh one from the request a revived
     * round sends it, rather than carrying the spent pass over.
     *
     * The allowance is one unspent pass per peer, renewed by each request sent
     * to it, since that peer's reply can still be in flight when the round
     * settles. A revival renews nothing by itself, so what hands out the fresh
     * pass here is the broadcast the restarted timer issues, which reaches
     * every peer the set tracks. The case waits for that broadcast rather than
     * assuming it, since only recordAsked() grants a pass.
     *
     * @param env The environment to run in.
     */
    void
    testARevivedRoundsRequestRenewsALateReplyPass(jtx::Env& env)
    {
        testcase("A revived round's request renews a peer's late-reply pass");

        // Inner nodes only, so the set stays incomplete and the acquisition keeps asking.
        DeepChain const chain{nextSeed()};

        // One peer asked, so it is the one whose pass is under test in both rounds.
        auto const candidate = std::make_shared<ChargeRecordingPeer>();
        auto peerSet =
            std::make_unique<RequestCountingPeerSet>(std::vector<std::shared_ptr<Peer>>{candidate});
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        // init() asks candidate, which is the request that earns it the first round's pass, and
        // the set tracks it from here, so a later broadcast reaches it.
        acquire->init(1);
        BEAST_EXPECT(peerSetPtr->addedPeers() == std::set<Peer::ID>{candidate->id()});

        // The first round fails, and candidate's one allowed late reply arrives and is free,
        // spending the pass that request earned it.
        acquire->cancel();
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges().empty());

        // Revived: the map is still valid, so stillNeed() clears the failure and restarts the
        // timer chain. The spent pass stands at this point.
        int const broadcastsBeforeRevival = peerSetPtr->broadcasts();
        BEAST_EXPECT(acquire->stillNeed());

        // kNormTimeouts intervals in, onTimer() broadcasts with no peer of its own, which
        // recordAsked() reads as a request to every peer the set tracks. That request is what
        // renews candidate's pass. Read as a broadcast rather than as a request, since a request
        // here would mean a peer was named.
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->broadcasts() > broadcastsBeforeRevival; }));

        // The second round fails too, with that renewed pass still unspent.
        acquire->cancel();

        // candidate's late reply here spends the pass the broadcast above renewed. This
        // assertion is what pins that renewal.
        BEAST_EXPECT(
            wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, candidate)));
        BEAST_EXPECT(candidate->charges().empty());
    }

    /**
     * Across a revival, only a peer the new round actually asks has its
     * late-reply pass renewed.
     *
     * requestedPeers_ also holds the unsolicited senders trigger() answered
     * directly, and PeerSetImpl::sendRequest does not start tracking those, so
     * a revived round's broadcast cannot reach them. One of those senders keeps
     * the pass it already spent.
     *
     * @param env The environment to run in.
     */
    void
    testRevivalRenewsOnlyAnAskedPeersPass(jtx::Env& env)
    {
        testcase("Only a peer a revived round asks has its late-reply pass renewed");

        // Inner nodes only, so one root leaves the set incomplete and the acquisition keeps
        // asking.
        DeepChain const chain{nextSeed()};

        // No candidates, so the set tracks nobody and a revived round's broadcast reaches nobody.
        auto peerSet = std::make_unique<RequestCountingPeerSet>();
        auto* const peerSetPtr = peerSet.get();

        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::move(peerSet), kFastRetry);

        acquire->init(1);

        // An unsolicited sender supplies the root. takeNodes() trigger()s it directly, which
        // records it as asked and sends it a request of its own, without the set tracking it.
        auto const stranger = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger).isUseful());
        BEAST_EXPECT(peerSetPtr->addedPeers().empty());

        // The round settles, and stranger spends the one pass that request earned it.
        acquire->cancel();
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges().empty());
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges() == std::vector{resource::kFeeUselessData});

        // Revived, then settled again. The set tracks no peer, so nothing was asked in between.
        BEAST_EXPECT(acquire->stillNeed());
        acquire->cancel();

        // No request reached stranger, so it has no fresh pass to spend.
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges().size() == 2);
    }

    /**
     * A fresh request renews an untracked peer's late-reply pass.
     *
     * trigger() answers an unsolicited sender directly, and
     * PeerSetImpl::sendRequest does not start tracking it, so a revived round's
     * broadcast cannot reach it. Recording that direct request is what owes it
     * a fresh pass, since the reply being judged answers the request just sent
     * to it.
     *
     * @param env The environment to run in.
     */
    void
    testADirectRequestRenewsAnUntrackedPeersPass(jtx::Env& env)
    {
        testcase("A direct request renews an untracked peer's late-reply pass");

        // Inner nodes only, so the root alone leaves the set incomplete and the acquisition keeps
        // asking.
        DeepChain const chain{nextSeed()};

        // No candidates, so the set tracks nobody and only a direct request reaches stranger.
        auto const acquire = std::make_shared<TransactionAcquire>(
            env.app(),
            chain.rootHash.asUInt256(),
            std::make_unique<RequestCountingPeerSet>(),
            kFastRetry);

        acquire->init(1);

        // The root, unsolicited, so trigger() sends stranger a request of its own.
        auto const stranger = std::make_shared<ChargeRecordingPeer>();
        BEAST_EXPECT(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger).isUseful());

        // The round settles, and stranger spends the pass that request earned it.
        acquire->cancel();
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger)));
        BEAST_EXPECT(stranger->charges().empty());

        // Revived, and a deeper node from stranger earns it another direct request.
        BEAST_EXPECT(acquire->stillNeed());
        BEAST_EXPECT(acquire->takeNodes({{chain.idAt(1), chain.nodeAt(1)}}, stranger).isUseful());

        // That request owes a fresh pass, so answering it once the set settles again is free.
        acquire->cancel();
        BEAST_EXPECT(wasIgnored(acquire->takeNodes({{chain.idAt(1), chain.nodeAt(1)}}, stranger)));
        BEAST_EXPECT(stranger->charges().empty());
    }

    /**
     * A charge reaches the peer when the reply arrives through gotData().
     *
     * The charge decision sits in takeNodes(), so this entry point's dispatch
     * has to carry it.
     *
     * @param env The environment to run in.
     */
    void
    testGotDataChargesThroughTakeNodes(jtx::Env& env)
    {
        testcase("A charge reaches the peer through the gotData() dispatch");

        DeepChain const chain{nextSeed()};
        auto& inbound = env.app().getInboundTransactions();

        UInt256 const setHash = chain.rootHash.asUInt256();
        BEAST_EXPECT(inbound.getSet(setHash, true) == nullptr);

        // The root is what the acquisition asked for, so it costs nothing.
        auto const peer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(setHash, peer, packetFor(chain, {{SHAMapNodeID{}, chain.nodeAt(0)}}));
        BEAST_EXPECT(peer->charges().empty());

        // A chain node labeled at a position it cannot occupy. The map stays sound, so this is the
        // merely-wrong tier.
        inbound.gotData(
            setHash, peer, packetFor(chain, {{SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1)}}));
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});
    }

    /**
     * A reply whose node data cannot be parsed is charged for.
     *
     * gotData() rejects the packet before the acquisition is handed anything,
     * so the charge is the dispatch layer's. Also the control for this suite's
     * "was not charged" assertions, which a harness recording no charge at all
     * would satisfy.
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
     * Each peer is charged for its own packet.
     *
     * takeNodes() classifies and charges under one lock hold, so each verdict
     * belongs to the packet that earned it rather than to the map's state once
     * a concurrent packet has been applied.
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
        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, rootPeer));
        BEAST_EXPECT(rootPeer->charges().empty());

        // A misplaced node. The map survives, so the generic tier.
        auto const wrongPeer = std::make_shared<ChargeRecordingPeer>();
        static_cast<void>(
            acquire->takeNodes({{SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1)}}, wrongPeer));
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});

        // Now a second peer invalidates the map, which must leave the earlier verdicts alone.
        auto const fabricatingPeer = std::make_shared<ChargeRecordingPeer>();
        static_cast<void>(acquire->takeNodes(chain.nodesBelowRoot(), fabricatingPeer));
        BEAST_EXPECT(fabricatingPeer->charges() == std::vector{resource::kFeeMalformedData});
        BEAST_EXPECT(!acquire->isMapValid());

        // The earlier peers' charges are untouched.
        BEAST_EXPECT(wrongPeer->charges() == std::vector{resource::kFeeInvalidData});
        BEAST_EXPECT(rootPeer->charges().empty());
    }

    /**
     * A batch that ends on a bad node still counts the good nodes ahead of it,
     * and a batch of nodes already held still counts as an answer.
     *
     * Recorded progress stops the next timer tick from counting a timeout. The
     * flag is read rather than the returned tally, and cleared between batches
     * so each reading is about the batch just fed.
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

        // The bad node is still charged for, at the recoverable tier.
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // A batch of nothing but the root we already have: counted as a duplicate, so not useful,
        // but it still postpones the timeout since this peer is one we asked and it answered.
        acquire->clearProgress();
        auto const repeatedRoot = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);

        BEAST_EXPECTS(tallyIs(repeatedRoot, 0, 0, 1), repeatedRoot.get());
        BEAST_EXPECT(repeatedRoot.isGood());
        BEAST_EXPECT(!repeatedRoot.isUseful());
        BEAST_EXPECT(acquire->madeProgress());

        // The same root alongside a node we do need: the duplicate is reported as one, and the
        // batch records progress. Cleared first, so the reading below is about this batch.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> mixed;
        mixed.emplace_back(SHAMapNodeID{}, chain.nodeAt(0));
        mixed.emplace_back(chain.idAt(3), chain.nodeAt(3));

        acquire->clearProgress();
        auto const withDuplicateRoot = acquire->takeNodes(mixed, peer);

        BEAST_EXPECTS(tallyIs(withDuplicateRoot, 1, 0, 1), withDuplicateRoot.get());
        BEAST_EXPECT(withDuplicateRoot.isUseful());
        BEAST_EXPECT(acquire->madeProgress());

        // None of that cost the sender anything beyond the one bad node above.
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});
    }

    /**
     * A duplicate-only reply from a peer we asked still postpones the timeout.
     *
     * On a fan-out the slower responder's reply is entirely nodes the faster
     * one already supplied. The flag asks whether the peers being waited on
     * are answering, and such a peer has.
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateOnlyReplyFromAskedPeerCountsAsProgress(jtx::Env& env)
    {
        testcase("A duplicate-only reply from a peer we asked counts as progress");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());

        // Two peers racing the same request. Each earns its place in requestedPeers_ the way a real
        // one does: a reply that lands buys a targeted follow-up request, which enrolls it.
        auto const slower = std::make_shared<ChargeRecordingPeer>();
        auto const faster = std::make_shared<ChargeRecordingPeer>();

        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, slower));

        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> below;
        below.emplace_back(chain.idAt(1), chain.nodeAt(1));
        below.emplace_back(chain.idAt(2), chain.nodeAt(2));

        auto const won = acquire->takeNodes(below, faster);
        BEAST_EXPECTS(tallyIs(won, 2, 0, 0), won.get());
        BEAST_EXPECT(acquire->madeProgress());

        // The same nodes from the peer that lost the race: all duplicates, and still an answer.
        acquire->clearProgress();
        auto const lost = acquire->takeNodes(below, slower);

        BEAST_EXPECTS(tallyIs(lost, 0, 0, 2), lost.get());
        BEAST_EXPECT(!lost.isUseful());
        BEAST_EXPECT(lost.isGood());
        BEAST_EXPECT(acquire->madeProgress());

        // Answering costs neither peer anything.
        BEAST_EXPECT(slower->charges().empty());
        BEAST_EXPECT(faster->charges().empty());

        acquire->cancel();
    }

    /**
     * Duplicates postpone a timeout only for a peer we asked, and only while
     * the acquisition is still running.
     *
     * A settled acquisition has no timer left to postpone, and reports a late
     * reply as a duplicate too, so the tally alone cannot tell the two apart.
     *
     * @param env The environment to run in.
     */
    void
    testUnaskedDuplicatesCannotPostponeTimeout(jtx::Env& env)
    {
        testcase("Duplicates postpone a timeout only for a peer we asked");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());

        auto const asked = std::make_shared<ChargeRecordingPeer>();
        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, asked));
        BEAST_EXPECT(acquire->madeProgress());

        // A peer no request ever went to, resending the root we already have.
        auto const stranger = std::make_shared<ChargeRecordingPeer>();
        acquire->clearProgress();
        auto const unsolicited = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger);

        BEAST_EXPECTS(tallyIs(unsolicited, 0, 0, 1), unsolicited.get());
        BEAST_EXPECT(!acquire->madeProgress());

        // Free as well, since a node that hashes into the map is data the acquisition asked for.
        BEAST_EXPECT(stranger->charges().empty());

        // Accepting that reply made trigger() request the missing nodes from this peer, which
        // enrolls it in requestedPeers_. So its next duplicate does count.
        acquire->clearProgress();
        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, stranger));
        BEAST_EXPECT(acquire->madeProgress());

        acquire->cancel();

        // A settled acquisition: a late reply from its own supplier, a peer in requestedPeers_, is
        // reported as a duplicate and still records nothing.
        auto const finished = DeepChain::toLeaf(3, nextSeed());
        auto const completed = std::make_shared<TestableTransactionAcquire>(
            env.app(), finished.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());
        auto const supplier = std::make_shared<ChargeRecordingPeer>();

        static_cast<void>(completed->takeNodes({{SHAMapNodeID{}, finished.nodeAt(0)}}, supplier));
        static_cast<void>(completed->takeNodes(finished.nodesBelowRoot(), supplier));

        completed->clearProgress();
        auto const late = completed->takeNodes({{SHAMapNodeID{}, finished.nodeAt(0)}}, supplier);

        BEAST_EXPECT(wasIgnored(late));
        BEAST_EXPECT(!completed->madeProgress());
    }

    /**
     * A peer replaying nodes we already hold runs out of duplicate credit.
     *
     * A duplicate-only reply postpones at most kMaxDuplicateCredits
     * consecutive intervals, after which the timeout count advances. Each
     * clearProgress() below stands in for the timer tick that clears the flag.
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateCreditRunsOut(jtx::Env& env)
    {
        testcase("A duplicate-only reply stops counting once its credit runs out");

        // kMaxDuplicateCredits in TransactionAcquire.cpp, which is file-local there.
        static constexpr int kMaxCredits = 4;

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());

        // The peer earns its place in requestedPeers_ the way a real one does: an accepted reply
        // buys a targeted follow-up request, which enrolls it.
        auto const replaying = std::make_shared<ChargeRecordingPeer>();
        auto const root = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, replaying);
        BEAST_EXPECTS(tallyIs(root, 1, 0, 0), root.get());

        // One node below the root, accepted once so every later copy of it is a duplicate.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> const below{
            {chain.idAt(1), chain.nodeAt(1)}};
        auto const accepted = acquire->takeNodes(below, replaying);
        BEAST_EXPECTS(tallyIs(accepted, 1, 0, 0), accepted.get());

        // Every credit the budget holds, one per interval. A useful node would earn the budget
        // back, so there is none between these.
        for (int credit = 0; credit < kMaxCredits; ++credit)
        {
            acquire->clearProgress();
            auto const replay = acquire->takeNodes(below, replaying);
            BEAST_EXPECTS(tallyIs(replay, 0, 0, 1), replay.get());
            BEAST_EXPECT(acquire->madeProgress());
        }

        // One past the budget. The reply is still a duplicate and still free, and it no longer
        // postpones the timeout.
        acquire->clearProgress();
        auto const spent = acquire->takeNodes(below, replaying);
        BEAST_EXPECTS(tallyIs(spent, 0, 0, 1), spent.get());
        BEAST_EXPECT(!acquire->madeProgress());

        BEAST_EXPECT(replaying->charges().empty());

        acquire->cancel();
    }

    /**
     * A reply mixing duplicates with a rejected node earns no duplicate credit.
     *
     * The tally spans the whole batch, so a duplicate ahead of a recoverable
     * bad node is still reported. Such a reply supplied no useful node and did
     * supply invalid data, so it must not postpone the timeout.
     *
     * @param env The environment to run in.
     */
    void
    testDuplicateWithRejectedNodeEarnsNoCredit(jtx::Env& env)
    {
        testcase("A duplicate alongside a rejected node earns no duplicate credit");

        DeepChain const chain{nextSeed()};

        auto const acquire = std::make_shared<TestableTransactionAcquire>(
            env.app(), chain.rootHash.asUInt256(), std::make_unique<RequestCountingPeerSet>());
        auto const peer = std::make_shared<ChargeRecordingPeer>();

        // The root, then the two nodes below it. Accepting them enrolls this peer in
        // requestedPeers_, which is what a duplicate credit requires, and leaves the budget full.
        static_cast<void>(acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer));

        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> below;
        below.emplace_back(chain.idAt(1), chain.nodeAt(1));
        below.emplace_back(chain.idAt(2), chain.nodeAt(2));
        BEAST_EXPECT(acquire->takeNodes(below, peer).isUseful());

        // The root we already hold, then a further chain node mislabeled at a position only a
        // leaf may occupy. The map stays sound, so the batch stops there and reports both.
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> mixed;
        mixed.emplace_back(SHAMapNodeID{}, chain.nodeAt(0));
        mixed.emplace_back(SHAMapNodeID{9, UInt256{}}, chain.nodeAt(3));

        acquire->clearProgress();
        auto const san = acquire->takeNodes(mixed, peer);

        BEAST_EXPECTS(tallyIs(san, 0, 1, 1), san.get());
        BEAST_EXPECT(!san.isUseful());
        BEAST_EXPECT(san.isInvalid());

        // No useful node and invalid data in the same reply, so no credit and no postponement.
        BEAST_EXPECT(!acquire->madeProgress());

        // The map survives, and the bad node is charged at the recoverable tier.
        BEAST_EXPECT(acquire->isMapValid());
        BEAST_EXPECT(peer->charges() == std::vector{resource::kFeeInvalidData});

        // The credit was withheld rather than spent, so a clean duplicate reply still counts.
        acquire->clearProgress();
        auto const clean = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECTS(tallyIs(clean, 0, 0, 1), clean.get());
        BEAST_EXPECT(acquire->madeProgress());

        acquire->cancel();
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
        // acquisition fails on its own, as in testTimerBroadcastsThenGivesUp.
        DeepChain const wrongChain{nextSeed()};
        auto const probe = [&] {
            return wasIgnored(acquire->takeNodes(
                {{SHAMapNodeID{}, wrongChain.nodeAt(0)}}, std::make_shared<ChargeRecordingPeer>()));
        };

        // Progress stays clear, so onTimer() counts a timeout every tick. Past kMaxTimeouts (20) it
        // fails itself and stops examining data.
        BEAST_EXPECT(waitFor(probe));
        int const requestsBeforeRevival = peerSetPtr->requests();

        // Revived, so the timer chain restarts. stillNeed() clamps timeouts_ to kNormTimeouts
        // rather than to zero, so the next tick broadcasts to every peer already tracked - the
        // only way one selected once is asked again.
        BEAST_EXPECT(acquire->stillNeed());
        BEAST_EXPECT(waitFor([&] { return peerSetPtr->requests() > requestsBeforeRevival; }));

        // Data is examined again: the real root is accepted, asks for the next level, and is free.
        auto const peer = std::make_shared<ChargeRecordingPeer>();
        auto const revived = acquire->takeNodes({{SHAMapNodeID{}, chain.nodeAt(0)}}, peer);
        BEAST_EXPECT(!wasIgnored(revived));
        BEAST_EXPECT(revived.isUseful());
        BEAST_EXPECT(peer->charges().empty());

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
        // the timeout count, so the acquisition keeps running.
        auto const askAgainRepeatedly = [&] {
            static_cast<void>(acquire->stillNeed());
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            return peerSetPtr->requests() > requestsFromInit;
        };

        // A tick gets through despite the asking, which is what the kept wait allows.
        BEAST_EXPECT(waitFor(askAgainRepeatedly));

        acquire->cancel();
    }

    /**
     * A retention window is refreshed only while the set is still needed.
     *
     * getSet() refreshes the window once per round, which keeps an entry out of
     * newRound()'s reach. A set no peer can complete is left unrefreshed and
     * swept. Both halves share the Env's one InboundTransactions, so they check
     * each other.
     *
     * Must run after every other case that registers a set, since newRound()
     * sweeps the whole map.
     *
     * @param env The environment to run in.
     */
    void
    testRetentionWindowIsRefreshedOnlyWhileNeeded(jtx::Env& env)
    {
        testcase("A retention window is refreshed only while the set is needed");

        // kSetKeepRounds in InboundTransactions.cpp, which is file-local there. One round past it
        // is what drops an entry left behind out of the window.
        static constexpr std::uint32_t kKeepRounds = 3;

        // Well past the round jtx::Env's own startRound() established, so the arithmetic below
        // cannot underflow and both entries start at this round.
        static constexpr std::uint32_t kBaseSeq = 100;

        auto& inbound = env.app().getInboundTransactions();
        inbound.newRound(kBaseSeq);

        // The set still worth waiting for: the root alone leaves it incomplete, so the acquisition
        // keeps running and stillNeed() keeps saying yes.
        auto const liveChain = DeepChain::toLeaf(3, nextSeed());
        UInt256 const liveHash = liveChain.rootHash.asUInt256();
        auto const livePeer = std::make_shared<ChargeRecordingPeer>();

        BEAST_EXPECT(inbound.getSet(liveHash, true) == nullptr);
        inbound.gotData(
            liveHash, livePeer, packetFor(liveChain, {{SHAMapNodeID{}, liveChain.nodeAt(0)}}));
        BEAST_EXPECT(livePeer->charges().empty());

        // The set no peer can ever complete: a chain ending in an inner node at kLeafDepth leaves
        // the map invalid, which fails the acquisition for every peer.
        DeepChain const deadChain{nextSeed()};
        UInt256 const deadHash = deadChain.rootHash.asUInt256();
        auto const deadPeer = std::make_shared<ChargeRecordingPeer>();

        BEAST_EXPECT(inbound.getSet(deadHash, true) == nullptr);
        inbound.gotData(
            deadHash, deadPeer, packetFor(deadChain, {{SHAMapNodeID{}, deadChain.nodeAt(0)}}));
        BEAST_EXPECT(deadPeer->charges().empty());

        // A different peer breaks the set, so deadPeer's own one free late reply stays unspent and
        // the probe below reads whether the entry is there rather than whether that pass is gone.
        auto const fabricatingPeer = std::make_shared<ChargeRecordingPeer>();
        inbound.gotData(
            deadHash, fabricatingPeer, packetFor(deadChain, deadChain.nodesBelowRoot()));
        BEAST_EXPECT(fabricatingPeer->charges() == std::vector{resource::kFeeMalformedData});

        // An ask once per round is the only thing that refreshes a window. Stops one round short of
        // the judging round below, since an ask on an entry already swept would register a fresh
        // acquisition and hide the sweep.
        for (std::uint32_t round = kBaseSeq + 1; round <= kBaseSeq + kKeepRounds; ++round)
        {
            inbound.newRound(round);
            BEAST_EXPECT(inbound.getSet(liveHash, true) == nullptr);
            BEAST_EXPECT(inbound.getSet(deadHash, true) == nullptr);
        }

        // One round past the window. An entry still sitting at kBaseSeq has fallen out of it, and
        // one the asks above moved forward has not.
        inbound.newRound(kBaseSeq + kKeepRounds + 1);

        // A reply for a hash the map still holds reaches the acquisition, and one for a hash it has
        // dropped is turned away by gotData(). Neither peer has spent its free late reply, so
        // kFeeUselessData here can only be the no-acquisition charge.
        inbound.gotData(
            liveHash, livePeer, packetFor(liveChain, {{SHAMapNodeID{}, liveChain.nodeAt(0)}}));
        BEAST_EXPECT(livePeer->charges().empty());

        inbound.gotData(
            deadHash, deadPeer, packetFor(deadChain, {{SHAMapNodeID{}, deadChain.nodeAt(0)}}));
        BEAST_EXPECT(deadPeer->charges() == std::vector{resource::kFeeUselessData});
    }

    /**
     * The retry timer broadcasts with no peer of its own, then gives up.
     *
     * Pins the two behaviors rather than the thresholds they trip at, since
     * bounding those means asserting on wall clock. What is pinned is that
     * onTimer() issues the broadcast, not that it reaches anyone.
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
        testLateReplyAllowanceSurvivesGiveSet(env);
        testLateReplyIsTurnedAwayBeforeParsing(env);
        testFabricatedChainFailsAcquire(env);
        testWrongNodeKeepsAcquireAlive(env);
        testBadRootKeepsAcquireAlive(env);
        testEmptyReplyIsCharged(env);
        testFeeTierDistinguishesFabrication(env);
        testDuplicateRootReplyIsFree(env);
        testDuplicateNonRootReplyIsFree(env);
        testLateReplyIsFreeOncePerPeerAsked(env);
        testSettledReplyEarnsNoProgress(env);
        testLateReplyPassIsKeyedByPeer(env);
        testARevivedRoundsRequestRenewsALateReplyPass(env);
        testRevivalRenewsOnlyAnAskedPeersPass(env);
        testADirectRequestRenewsAnUntrackedPeersPass(env);
        testUndeserializableNodeIsCharged(env);
        testGotDataChargesThroughTakeNodes(env);
        testChargeIsNotDecidedAfterTheLock(env);
        testPartialBatchIsCounted(env);
        testDuplicateOnlyReplyFromAskedPeerCountsAsProgress(env);
        testUnaskedDuplicatesCannotPostponeTimeout(env);
        testDuplicateCreditRunsOut(env);
        testDuplicateWithRejectedNodeEarnsNoCredit(env);
        testInitFiltersCandidatesByHasTxSet(env);
        testStillNeedLeavesARunningAcquireAlone(env);

        // Last of the cases that use the Env's InboundTransactions: newRound() sweeps its whole
        // map, so anything above that still expects an entry it registered must run first.
        testRetentionWindowIsRefreshedOnlyWhileNeeded(env);

        // Last: both wait out a whole timeout chain.
        testRevivedAcquireCanRequestAgain(env);
        testTimerBroadcastsThenGivesUp(env);
    }

private:
    unsigned int seed_{0};
};

BEAST_DEFINE_TESTSUITE(TransactionAcquire, app, xrpl);

}  // namespace xrpl::test
