#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/DeepChain.h>
#include <shamap/InnerNode.h>
#include <shamap/common.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::tests {

// The cap on how many nodes a walk reports, above what any test here expects.
static constexpr int kMaxNodesPerRequest = 2048;

/**
 * Rules with no amendments enabled.
 *
 * @return The rules.
 */
[[nodiscard]] static Rules
noAmendments()
{
    return Rules{std::unordered_set<UInt256, beast::Uhash<>>{}};
}

/**
 * Whether a verdict carries exactly the given counts.
 *
 * The counts rather than get(): that string is a log format, not an API. It is
 * pinned once, in the SHAMapAddNode tests, and read here only to describe a
 * failure.
 *
 * @param san The verdict to check.
 * @param good How many nodes the batch should have hooked in.
 * @param bad How many it should have rejected.
 * @param duplicate How many it should have already held.
 * @return Whether the verdict matches, naming the actual tally if it does not.
 */
[[nodiscard]] static ::testing::AssertionResult
tallyIs(SHAMapAddNode const& san, int good, int bad, int duplicate)
{
    if (san.getGood() == good && san.getBad() == bad && san.getDuplicate() == duplicate)
        return ::testing::AssertionSuccess();

    return ::testing::AssertionFailure() << "tally is " << san.get() << ", expected good:" << good
                                         << " bad:" << bad << " dupe:" << duplicate;
}

class SHAMapSyncTest : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};
    beast::XorShiftEngine eng_;

    boost::intrusive_ptr<SHAMapItem>
    makeRandomAS()
    {
        static constexpr auto kWordsPerState = 3uz;

        Serializer s;

        for (auto word = 0uz; word < kWordsPerState; ++word)
            s.add32(randInt<std::uint32_t>(eng_));
        return makeShamapitem(s.getSHA512Half(), s.slice());
    }

    bool
    confuseMap(SHAMap& map, std::size_t count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        SHAMapHash const beforeHash = map.getHash();

        std::list<UInt256> items;

        for (auto i = 0uz; i < count; ++i)
        {
            auto item = makeRandomAS();
            items.push_back(item->key());

            if (!map.addItem(SHAMapNodeType::TnAccountState, item))
            {
                ADD_FAILURE() << "Unable to add item to map";
                return false;
            }
        }

        for (auto const& item : items)
        {
            if (!map.delItem(item))
            {
                ADD_FAILURE() << "Unable to remove item from map";
                return false;
            }
        }

        if (beforeHash != map.getHash())
        {
            ADD_FAILURE() << "Hashes do not match " << beforeHash << " " << map.getHash();
            return false;
        }

        return true;
    }

    /**
     * A sync filter that records every node it is told about, and serves back
     * only the ones it was explicitly asked to hold.
     *
     * Serving is opt-in: the sync path consults the filter before deciding
     * a node is missing, so each case serves only the node it wants resolved.
     */
    class RecordingFilter : public SHAMapSyncFilter
    {
    public:
        // What one gotNode() call was told, in the order the calls arrived.
        struct Report
        {
            bool fromFilter;
            SHAMapHash hash;
            std::uint32_t ledgerSeq;
        };

        void
        gotNode(
            bool fromFilter,
            SHAMapHash const& hash,
            std::uint32_t ledgerSeq,
            Blob&&,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            SHAMapNodeType) const override
        {
            reports_.push_back({.fromFilter = fromFilter, .hash = hash, .ledgerSeq = ledgerSeq});
        }

        [[nodiscard]] std::optional<Blob>
        getNode(SHAMapHash const& hash) const override
        {
            if (auto const it = served_.find(hash); it != served_.end())
                return it->second;
            return std::nullopt;
        }

        /**
         * Offer a node back to the map, as a fetch pack does.
         *
         * @param node The node to serve, keyed by its own hash.
         */
        void
        serve(SHAMapTreeNodePtr const& node)
        {
            Serializer s;
            node->serializeWithPrefix(s);
            served_.emplace(node->getHash(), s.modData());
        }

        [[nodiscard]] std::vector<Report> const&
        reports() const
        {
            return reports_;
        }

    private:
        // Mutable because the whole interface is const: a filter is handed to the map by
        // const pointer, so recording has to happen through one.
        mutable std::vector<Report> reports_;
        std::map<SHAMapHash, Blob> served_;
    };

    /**
     * A sync filter that serves a range of a DeepChain's nodes, by hash.
     *
     * Stands in for a fetch pack, which is checked against each node's own
     * hash alone, so a walk resolves nodes locally through the filter
     * rather than through addKnownNode(). Anything outside the range,
     * including a decoy child, looks unavailable.
     */
    class ChainFilter : public SHAMapSyncFilter
    {
    public:
        /**
         * @param chain The chain whose nodes to serve.
         * @param maxDepth The deepest node to serve.
         * @param minDepth The shallowest node to serve.
         */
        explicit ChainFilter(
            DeepChain const& chain,
            unsigned int maxDepth = SHAMap::kLeafDepth,
            unsigned int minDepth = 0)
        {
            for (auto depth = minDepth; depth <= maxDepth; ++depth)
                nodes_.emplace(chain.nodeAt(depth)->getHash(), chain.prefixedNodeAt(depth));
        }

        void
        gotNode(
            bool,
            SHAMapHash const&,
            std::uint32_t,
            Blob&&,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            SHAMapNodeType) const override
        {
        }

        [[nodiscard]] std::optional<Blob>
        getNode(SHAMapHash const& hash) const override
        {
            if (auto const it = nodes_.find(hash); it != nodes_.end())
                return it->second;
            return std::nullopt;
        }

    private:
        std::map<SHAMapHash, Blob> nodes_;
    };

    /**
     * A root inner node with all 16 branches occupied and not one of them
     * resolvable.
     *
     * A walk of a backed map posts an asynchronous read for every branch in a
     * single pass, so the nodestore reader threads run finishFetch() for the
     * same map at the same time.
     */
    struct WideRoot
    {
        SHAMapTreeNodePtr node;
        SHAMapHash hash;

        WideRoot()
        {
            std::vector<InnerChild> children;
            children.reserve(SHAMap::kBranchFactor);

            for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
            {
                // Derived from the branch, so each posts its own read and no hash collides with a
                // real node's.
                UInt256 childHash;
                childHash.begin()[0] = 0xFA;
                childHash.begin()[1] = 0xB1;
                childHash.begin()[2] = static_cast<unsigned char>(branch);
                children.push_back({.branch = branch, .hash = SHAMapHash{childHash}});
            }

            node = makeFullInnerNode(children);
            hash = node->getHash();
        }
    };

    /**
     * A root inner node naming one leaf's hash under two branches: the one the
     * leaf's key selects and the next one over.
     *
     * Nothing in the root's hash contradicts the second reference, since the
     * wire form is a list of child hashes with no claim about the keys below
     * them. Only the leaf's own key tells the two references apart.
     */
    struct SharedLeafRoot
    {
        // The leaf one level down, so the root names it directly.
        DeepChain chain{DeepChain::toLeaf(1)};
        SHAMapTreeNodePtr leaf{chain.nodeAt(1)};

        unsigned int realBranch{selectBranch(SHAMapNodeID{}, chain.pathKey)};
        unsigned int otherBranch{(realBranch + 1) % SHAMap::kBranchFactor};

        SHAMapTreeNodePtr root{makeCompressedInnerNode(
            {{.branch = realBranch, .hash = leaf->getHash()},
             {.branch = otherBranch, .hash = leaf->getHash()}})};

        // Where each reference puts the leaf.
        SHAMapNodeID realID{chain.idAt(1)};
        SHAMapNodeID otherID{SHAMapNodeID{}.getChildNodeID(otherBranch)};
    };
};

// Only a leaf may sit at kLeafDepth. An inner node there is reported as bad data and leaves the
// map invalid.
TEST_F(SHAMapSyncTest, inner_node_at_leaf_depth)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));
    ASSERT_TRUE(map.isValid());

    auto const result = chain.addOffendingNode(map);

    EXPECT_TRUE(tallyIs(result, 0, 1, 0));
    EXPECT_FALSE(result.isGood());
    EXPECT_FALSE(map.isValid());

    // Invalid is terminal, so setImmutable() refuses.
    EXPECT_FALSE(map.setImmutable());
}

// A rejection says what the node did, so only the arm that proved the map impossible reports
// invalidatedMap(). InboundLedger::receiveNode() reads that rather than the map's current state,
// which a getMissingNodes() walk on another thread can flip while a packet is being judged.
//
// One family throughout: nothing here runs a walk, so the full-below cache it shares stays empty
// and no map's verdict can be answered from another's.
TEST_F(SHAMapSyncTest, only_the_map_invalidating_arm_reports_it)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    // Every refusal that leaves the map usable, on a map holding just the root.
    SHAMap sound{SHAMapType::FREE, f};
    sound.setSynching();
    ASSERT_TRUE(sound.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // A depth the node does not sit at.
    auto const wrongDepth =
        sound.addKnownNode(SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1), nullptr);
    EXPECT_TRUE(wrongDepth.isInvalid());
    EXPECT_FALSE(wrongDepth.invalidatedMap());

    // A branch the root leaves empty. The chain sits on branch 0 at every depth.
    UInt256 otherBranch;
    otherBranch.begin()[0] = 0x10;
    auto const emptyBranch =
        sound.addKnownNode(SHAMapNodeID{1, otherBranch}, chain.nodeAt(1), nullptr);
    EXPECT_TRUE(emptyBranch.isInvalid());
    EXPECT_FALSE(emptyBranch.invalidatedMap());

    // The right position, but data that hashes to something the root does not name there. This is
    // the arm the racing packet of the defect takes.
    auto const corrupt = sound.addKnownNode(SHAMapNodeID{1, UInt256{}}, chain.nodeAt(2), nullptr);
    EXPECT_TRUE(corrupt.isInvalid());
    EXPECT_FALSE(corrupt.invalidatedMap());

    // A root offered under a hash the map does not hold names another tree.
    auto const otherRoot = sound.addRootNode(chain.nodeAt(1)->getHash(), chain.nodeAt(0), nullptr);
    EXPECT_TRUE(otherRoot.isInvalid());
    EXPECT_FALSE(otherRoot.invalidatedMap());

    EXPECT_TRUE(sound.isValid());

    // A root whose data does not hash to the hash it is offered under, on a map with no root yet.
    SHAMap rootless{SHAMapType::FREE, f};
    rootless.setSynching();
    auto const corruptRoot =
        rootless.addRootNode(chain.nodeAt(1)->getHash(), chain.nodeAt(0), nullptr);
    EXPECT_TRUE(corruptRoot.isInvalid());
    EXPECT_FALSE(corruptRoot.invalidatedMap());
    EXPECT_TRUE(rootless.isValid());

    // The one arm that proves the map impossible: an inner node at kLeafDepth.
    SHAMap condemned{SHAMapType::FREE, f};
    condemned.setSynching();
    ASSERT_TRUE(chain.fill(condemned));
    ASSERT_TRUE(condemned.isValid());

    auto const offending = chain.addOffendingNode(condemned);
    EXPECT_TRUE(offending.isInvalid());
    EXPECT_TRUE(offending.invalidatedMap());
    EXPECT_FALSE(condemned.isValid());

    // The flag reads off a packet's running tally too, which is how receiveNode() would see it had
    // the offending node arrived behind an accepted one.
    SHAMapAddNode tally;
    tally += SHAMapAddNode::useful();
    EXPECT_FALSE(tally.invalidatedMap());
    tally += offending;
    EXPECT_TRUE(tally.invalidatedMap());
}

// A node the descent cannot hook in is bad data: the batch counts it bad and the map stays usable
// for another sender. All three refusals are covered: a depth the node does not sit at, a branch
// the root leaves empty, and a hash the root does not name.
TEST_F(SHAMapSyncTest, node_that_cannot_be_hooked_is_bad_data)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // nodeAt(1) is the node the root is missing and its hash matches, but we claim depth 2.
    auto const wrongDepth = map.addKnownNode(SHAMapNodeID{2, UInt256{}}, chain.nodeAt(1), nullptr);

    EXPECT_TRUE(tallyIs(wrongDepth, 0, 1, 0));
    EXPECT_FALSE(wrongDepth.isUseful());

    // The chain sits on branch 0 at every depth, so a node claiming a position on branch 1 asks the
    // descent to follow a branch the root leaves empty.
    UInt256 otherBranch;
    otherBranch.begin()[0] = 0x10;
    auto const emptyBranch =
        map.addKnownNode(SHAMapNodeID{1, otherBranch}, chain.nodeAt(1), nullptr);

    EXPECT_TRUE(tallyIs(emptyBranch, 0, 1, 0));

    // The right position this time, but the data hashes to something other than the child the root
    // says belongs there.
    auto const corrupt = map.addKnownNode(SHAMapNodeID{1, UInt256{}}, chain.nodeAt(2), nullptr);

    EXPECT_TRUE(tallyIs(corrupt, 0, 1, 0));

    // The verdict is bad data alone, so the map stays usable.
    EXPECT_TRUE(map.isValid());
}

// A root is installed once and a map is synced against one hash, so a root offered under a hash the
// map does not hold names another tree and is bad data. The same root under the hash the map does
// hold is the duplicate it is.
TEST_F(SHAMapSyncTest, add_root_node_judges_the_hash_asked_for)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // The same root under the hash the map holds: already held, and reported as such.
    auto const same = map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr);

    EXPECT_TRUE(tallyIs(same, 0, 0, 1));
    EXPECT_TRUE(same.isGood());

    // A hash the map does not hold. nodeAt(1) is a real node of the same chain, so this is a
    // well-formed hash that names another tree.
    auto const other = map.addRootNode(chain.nodeAt(1)->getHash(), chain.nodeAt(0), nullptr);

    EXPECT_TRUE(tallyIs(other, 0, 1, 0));
    EXPECT_FALSE(other.isGood());
    EXPECT_TRUE(other.isInvalid());

    // The refusal is about the hash asked for, so the root the map holds stays in place and the map
    // stays usable.
    EXPECT_EQ(map.getHash(), chain.rootHash);
    EXPECT_TRUE(map.isValid());
}

// The verdict outranks the full-below cache. That cache is keyed by node hash and shared by every
// map of a family, and a hash covers a node's children but not its depth, so an earlier walk can
// mark the same subtree hash complete at one depth while this map reaches it at kLeafDepth, with no
// collision involved. The descent therefore skips the lookup at that boundary and reaches the depth
// verdict first. This case seeds the entry a lookup would match, so dropping the skip turns the
// verdict back into a duplicate.
TEST_F(SHAMapSyncTest, map_invalidating_node_is_judged_before_the_cache_is_read)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));
    ASSERT_TRUE(map.isValid());

    // This case seeds the entry a lookup at the boundary would match: the offending node's hash
    // at its own position. The descent skips the lookup there, so the entry is never read and
    // the depth verdict stands. Drop the skip and the hit returns for the whole branch, so the
    // tally below becomes a duplicate.
    f.getFullBelowCache()->insert(
        chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256(), chain.idAt(SHAMap::kLeafDepth));

    auto const result = chain.addOffendingNode(map);

    EXPECT_TRUE(tallyIs(result, 0, 1, 0));
    EXPECT_FALSE(result.isGood());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// A full-below entry answers only for the position the walk that filed it finished at. One cache
// serves every map of a family, and a node's hash covers its children rather than its place, so a
// subtree completed under one node ID must not satisfy a lookup for the same hash under another.
// This case files a real subtree at its own position, has a root record that subtree one branch
// over, and checks that the walk descends and reaches the leaf below rather than taking the
// shortcut. An inner node fits any position, so the leaf is what the position test can refuse.
TEST_F(SHAMapSyncTest, full_below_entry_does_not_answer_at_another_position)
{
    TestNodeFamily f{j_};

    // A leaf two levels down, so the subtree filed below is an inner node with a real leaf under
    // it.
    auto const chain = DeepChain::toLeaf(2);
    auto const subtree = chain.nodeAt(1);

    auto const realBranch = selectBranch(0u, chain.pathKey);
    auto const otherBranch = (realBranch + 1) % SHAMap::kBranchFactor;

    // A walk of that chain would record this same entry. The subtree is complete, and the entry
    // sits at the subtree's own position.
    f.getFullBelowCache()->insert(subtree->getHash().asUInt256(), chain.idAt(1));

    // A root recording the same subtree one branch over. Nothing in the subtree's hash contradicts
    // that placement.
    auto const rootAtOtherBranch =
        makeCompressedInnerNode({{.branch = otherBranch, .hash = subtree->getHash()}});
    ASSERT_TRUE(rootAtOtherBranch != nullptr);

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();
    ASSERT_TRUE(map.addRootNode(rootAtOtherBranch->getHash(), rootAtOtherBranch, nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    // The path this root puts the chain's leaf on: the chain's own key with its first nibble
    // replaced. selectBranch reads the high nibble of a byte at an even depth and the low nibble at
    // an odd one, so rewriting the high nibble of byte 0 moves depth 0 alone.
    UInt256 keyThroughOtherBranch = chain.pathKey;
    keyThroughOtherBranch.begin()[0] =
        static_cast<unsigned char>((otherBranch << 4) | (chain.pathKey.begin()[0] & 0x0Fu));

    // Serves the subtree and the leaf under it, so the descent can resolve both.
    ChainFilter const filter{chain, 2, 1};

    // An inner node agrees with any position, so this one is judged by what the descent meets on
    // the way rather than by a label of its own.
    auto const offered = makeCompressedInnerNode({{.branch = 0u, .hash = SHAMapHash{UInt256{1}}}});
    ASSERT_TRUE(offered != nullptr);

    auto const result =
        map.addKnownNode(SHAMapNodeID::createID(2, keyThroughOtherBranch), offered, &filter);

    // The lookup missed, so the walk descended, and the chain's leaf does not belong under
    // otherBranch.
    EXPECT_FALSE(result.isGood());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// A leaf hooked at the branch its key selects, and named again one branch over, where nothing holds
// it. Every lookup the walk makes is keyed by hash alone, so only the pointer the root holds tells
// the resident leaf from the misplaced reference, and the walk has nothing to judge at the wrong
// branch. It reports that reference as missing and leaves the map Synching and valid. The node a
// peer then supplies for that position is the same leaf, which addKnownNode() refuses at the wrong
// branch and condemns the map for, without hooking it there.
//
// This pins the outcome for both visit orders, since the walk starts at a random branch. The wrong
// branch first leaves the leaf's hash in missingHashes, so the valid branch is skipped, where there
// was nothing to judge. The valid branch first judges the resident leaf, which belongs, and the
// wrong branch is then asked for on its own.
TEST_F(SHAMapSyncTest, shared_leaf_hash_is_judged_where_a_peer_answers_for_it)
{
    // Enough rounds that both visit orders are all but certain to run.
    static constexpr auto kRounds = 16uz;

    for (auto round = 0uz; round < kRounds; ++round)
    {
        TestNodeFamily f{j_};
        SharedLeafRoot const shared;
        ASSERT_TRUE(shared.root != nullptr);

        // Unbacked, with no filter, so the pointer the root holds is the only place the leaf can be
        // found.
        SHAMap map{SHAMapType::FREE, f};
        map.setUnbacked();
        map.setSynching();
        ASSERT_TRUE(map.addRootNode(shared.root->getHash(), shared.root, nullptr).isGood());

        // Hooked at the branch its key selects, a position addKnownNode() judges and accepts.
        ASSERT_TRUE(map.addKnownNode(shared.realID, shared.leaf, nullptr).isUseful());
        ASSERT_TRUE(map.isValid());

        // An unbacked map holds the root object it was offered, so this reads the map's own root.
        auto* const root = safeDowncast<SHAMapInnerNode*>(shared.root.get());
        ASSERT_TRUE(root->getChildPointer(shared.realBranch) == shared.leaf.get());
        ASSERT_TRUE(root->getChildPointer(shared.otherBranch) == nullptr);

        // The misplaced reference is the one thing the walk cannot resolve.
        auto const missing = map.getMissingNodes(kMaxNodesPerRequest, nullptr);
        ASSERT_EQ(missing.size(), 1u) << "round " << round;
        EXPECT_EQ(missing[0].first, shared.otherID) << "round " << round;
        EXPECT_EQ(missing[0].second, shared.leaf->getHash().asUInt256()) << "round " << round;
        EXPECT_TRUE(map.isValid()) << "round " << round;
        EXPECT_TRUE(map.isSynching()) << "round " << round;

        // The only bytes that hash to what was asked for are the leaf's, and its key does not sit
        // under otherBranch.
        auto const result = map.addKnownNode(shared.otherID, shared.leaf, nullptr);
        EXPECT_TRUE(result.isInvalid()) << "round " << round;
        EXPECT_TRUE(result.invalidatedMap()) << "round " << round;
        EXPECT_FALSE(map.isValid()) << "round " << round;
        EXPECT_TRUE(root->getChildPointer(shared.otherBranch) == nullptr) << "round " << round;
        EXPECT_TRUE(root->getChildPointer(shared.realBranch) == shared.leaf.get())
            << "round " << round;
    }
}

// The same root, with the leaf obtainable through the sync filter for both branches. The walk
// resolves it at the wrong branch, whichever branch it visits first, and the position test on the
// walk's filter path condemns the map there. Nothing is reported missing, and the map does not
// finish.
TEST_F(SHAMapSyncTest, shared_leaf_hash_resolved_at_the_wrong_branch_condemns_the_map)
{
    // Enough rounds that both visit orders are all but certain to run.
    static constexpr auto kRounds = 16uz;

    for (auto round = 0uz; round < kRounds; ++round)
    {
        TestNodeFamily f{j_};
        SharedLeafRoot const shared;
        ASSERT_TRUE(shared.root != nullptr);

        // Serves the leaf alone, by hash, so both branches resolve to it.
        ChainFilter const filter{shared.chain, 1, 1};

        SHAMap map{SHAMapType::FREE, f};
        map.setUnbacked();
        map.setSynching();
        ASSERT_TRUE(map.addRootNode(shared.root->getHash(), shared.root, &filter).isGood());

        auto const missing = map.getMissingNodes(kMaxNodesPerRequest, &filter);
        EXPECT_TRUE(missing.empty()) << "round " << round;
        EXPECT_FALSE(map.isValid()) << "round " << round;
        EXPECT_FALSE(map.setImmutable()) << "round " << round;
    }
}

// The full-below flag lives on the node object, which the TreeNodeCache shares by hash across maps
// and positions, so a flag one walk set says nothing about the same object hooked somewhere else.
// The flag is therefore written and read for a walk's root alone, and the position-keyed cache is
// the only memo below it. This case has an honest map complete a subtree, then checks that a root
// recording the same subtree one branch over descends it rather than reading the flag, and that the
// subtree, used as a root itself, is walked rather than trusted. The walk judges the leaf it then
// meets, which lies off its key at both positions, so both maps end Invalid.
TEST_F(SHAMapSyncTest, full_below_flag_answers_for_the_root_alone)
{
    TestNodeFamily f{j_};
    auto const gen = f.getFullBelowCache()->getGeneration();

    // A leaf two levels down, so the subtree completed below the root is an inner node with a real
    // leaf under it. The two depths select distinct branches, so moving the subtree moves the leaf
    // off its key.
    auto const chain = DeepChain::toLeaf(2);
    auto const subtree = chain.nodeAt(1);
    auto const realBranch = selectBranch(SHAMapNodeID{}, chain.pathKey);
    ASSERT_NE(realBranch, selectBranch(chain.idAt(1), chain.pathKey));

    // Serves the subtree and the leaf under it, so every walk here resolves both synchronously.
    ChainFilter const filter{chain, 2, 1};

    // The honest walk completes the subtree below its root: it files the position-keyed entry and
    // leaves the node flag alone, since the subtree is not this walk's root.
    SHAMap honest{SHAMapType::FREE, f};
    honest.setSynching();
    ASSERT_TRUE(honest.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
    ASSERT_TRUE(honest.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    ASSERT_FALSE(honest.isSynching());
    EXPECT_TRUE(f.getFullBelowCache()->touchIfExists(
        subtree->getHash().asUInt256(), SHAMapNodeID{}, realBranch));

    // The object every map in the family now shares for the subtree is the one the honest walk
    // canonicalized, not the chain's own instance, so the flag is read there.
    auto const shared = f.getTreeNodeCache()->fetch(subtree->getHash().asUInt256());
    ASSERT_TRUE(shared != nullptr);
    auto const* const sharedInner = safeDowncast<SHAMapInnerNode const*>(shared.get());
    EXPECT_FALSE(sharedInner->isFullBelow(gen));

    // A root recording the same subtree one branch over. The walk resolves the shared object from
    // the cache and descends it rather than reading its flag, then condemns the map at the leaf,
    // so no entry is filed for this position.
    auto const otherBranch = (realBranch + 1) % SHAMap::kBranchFactor;
    auto const rootAtOtherBranch =
        makeCompressedInnerNode({{.branch = otherBranch, .hash = subtree->getHash()}});
    ASSERT_TRUE(rootAtOtherBranch != nullptr);

    SHAMap shifted{SHAMapType::FREE, f};
    shifted.setSynching();
    ASSERT_TRUE(
        shifted.addRootNode(rootAtOtherBranch->getHash(), rootAtOtherBranch, nullptr).isGood());
    EXPECT_TRUE(shifted.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_FALSE(shifted.isValid());
    EXPECT_FALSE(shifted.setImmutable());
    EXPECT_FALSE(f.getFullBelowCache()->touchIfExists(
        subtree->getHash().asUInt256(), SHAMapNodeID{}, otherBranch));

    // The subtree as a root. Its flag is clear, so the walk runs, meets the leaf one level
    // shallower than its key says, and condemns the map, so the flag stays clear.
    SHAMap rooted{SHAMapType::FREE, f};
    rooted.setSynching();
    ASSERT_TRUE(rooted.addRootNode(subtree->getHash(), subtree, nullptr).isGood());
    EXPECT_TRUE(rooted.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_FALSE(rooted.isValid());
    EXPECT_FALSE(rooted.setImmutable());
    EXPECT_FALSE(sharedInner->isFullBelow(gen));
}

// An entry is filed under a node's own position and looked up from the node above it plus the
// branch taken, so the two ways of naming one position have to produce identical key bytes. The
// case above asserts only that a lookup one branch over misses, which a lookup that named nothing
// at all would also satisfy. This case asserts the hit, which is what ties the two forms together.
// Both nibble parities are covered, since a position's nibble is the high one at an even parent
// depth and the low one at an odd depth.
TEST_F(SHAMapSyncTest, full_below_lookup_finds_the_entry_an_insert_filed)
{
    TestNodeFamily f{j_};
    auto const cache = f.getFullBelowCache();

    // Inner nodes at depths 1 and 2 with a real leaf below them, which is the kind of subtree the
    // walk records.
    auto const chain = DeepChain::toLeaf(3);

    for (auto depth = 1u; depth <= 2u; ++depth)
    {
        auto const subtree = chain.nodeAt(depth);
        ASSERT_TRUE(subtree != nullptr) << "depth " << depth;
        auto const subtreeHash = subtree->getHash().asUInt256();

        // Filed under the node's own position, the way gmnProcessNodes files it.
        cache->insert(subtreeHash, chain.idAt(depth));

        // The lookup names the node above and the branch taken, the way both walks ask for it.
        auto const parentID = chain.idAt(depth - 1);
        auto const realBranch = selectBranch(parentID, chain.pathKey);

        for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
        {
            EXPECT_EQ(cache->touchIfExists(subtreeHash, parentID, branch), branch == realBranch)
                << "depth " << depth << " branch " << branch;
        }
    }
}

// An invalid transaction map stops the enclosing ledger from being marked immutable, since an
// immutable ledger is treated as persistable.
TEST_F(SHAMapSyncTest, invalid_tx_map_blocks_immutable_ledger)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    Ledger ledger{1, NetClock::time_point{}, noAmendments(), Fees{}, f};
    ASSERT_FALSE(ledger.isImmutable());

    ledger.txMap().setSynching();
    ASSERT_TRUE(chain.fill(ledger.txMap()));

    auto const result = chain.addOffendingNode(ledger.txMap());
    ASSERT_TRUE(tallyIs(result, 0, 1, 0));
    ASSERT_FALSE(ledger.txMap().isValid());

    // The state map is untouched, so only the transaction map can be refusing.
    ASSERT_TRUE(ledger.stateMap().isValid());

    EXPECT_FALSE(ledger.setImmutable());
    EXPECT_FALSE(ledger.isImmutable());
}

// The same for the state map, which is the second operand of the one expression
// Ledger::setImmutable() tests both maps in.
TEST_F(SHAMapSyncTest, invalid_state_map_blocks_immutable_ledger)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    Ledger ledger{1, NetClock::time_point{}, noAmendments(), Fees{}, f};
    ASSERT_FALSE(ledger.isImmutable());

    ledger.stateMap().setSynching();
    ASSERT_TRUE(chain.fill(ledger.stateMap()));

    auto const result = chain.addOffendingNode(ledger.stateMap());
    ASSERT_TRUE(tallyIs(result, 0, 1, 0));
    ASSERT_FALSE(ledger.stateMap().isValid());

    // The transaction map is untouched, so only the state map can be refusing.
    ASSERT_TRUE(ledger.txMap().isValid());

    EXPECT_FALSE(ledger.setImmutable());
    EXPECT_FALSE(ledger.isImmutable());
}

// A refusal leaves the header exactly as it was. setImmutable() derives the map hashes from the
// maps and then the ledger hash from the header, and writes them only after every check has
// passed. The up-front check covers this case, and the re-test after the maps are settled shares
// the rule, which is why the header is written only once that one has passed too.
TEST_F(SHAMapSyncTest, refused_settle_leaves_the_header_alone)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    // Not the header constructor: this one derives its map hashes, which is what must not happen.
    Ledger ledger{1, NetClock::time_point{}, noAmendments(), Fees{}, f};
    ASSERT_FALSE(ledger.isImmutable());
    ASSERT_TRUE(ledger.header().txHash.isZero());
    ASSERT_TRUE(ledger.header().accountHash.isZero());
    auto const hashBefore = ledger.header().hash;

    // A transaction map that hashes to something, so a derived header hash differs from the one
    // the ledger has now.
    ASSERT_TRUE(ledger.txMap().addItem(SHAMapNodeType::TnTransactionNm, makeRandomAS()));
    ASSERT_TRUE(ledger.txMap().getHash().isNonZero());

    // And a state map the chain abandons, so settling has to refuse.
    ledger.stateMap().setSynching();
    ASSERT_TRUE(chain.fill(ledger.stateMap()));
    ASSERT_TRUE(chain.addOffendingNode(ledger.stateMap()).isInvalid());
    ASSERT_FALSE(ledger.stateMap().isValid());

    EXPECT_FALSE(ledger.setImmutable());

    // The map hashes, the ledger hash and the flag all still hold their pre-refusal values.
    EXPECT_FALSE(ledger.isImmutable());
    EXPECT_TRUE(ledger.header().txHash.isZero());
    EXPECT_TRUE(ledger.header().accountHash.isZero());
    EXPECT_EQ(ledger.header().hash, hashBefore);
}

// A ledger built from a header must not claim to be immutable before setImmutable() has found both
// maps sound: they start out Synching and are filled in afterwards, and LedgerHistory::insert() and
// LedgerReplayMsgHandler both gate on that claim to catch exactly that case.
TEST_F(SHAMapSyncTest, ledger_from_header_is_not_immutable_until_settled)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    LedgerHeader header;
    header.seq = 2;
    header.txHash = chain.rootHash.asUInt256();
    header.hash = calculateLedgerHash(header);

    Ledger ledger{header, noAmendments(), f};

    // Both maps are still syncing, so the ledger reports mutable.
    EXPECT_TRUE(ledger.txMap().isSynching());
    EXPECT_TRUE(ledger.stateMap().isSynching());
    EXPECT_FALSE(ledger.isImmutable());

    ASSERT_TRUE(chain.fill(ledger.txMap()));
    ASSERT_TRUE(chain.addOffendingNode(ledger.txMap()).isInvalid());
    ASSERT_FALSE(ledger.txMap().isValid());

    EXPECT_FALSE(ledger.setImmutable());
    EXPECT_FALSE(ledger.isImmutable());
}

// The header's own map hashes are what the maps are synced against, so settling keeps them. The
// transaction map is left empty while the header names a chain root, so the two differ and the
// kept value is observable.
TEST_F(SHAMapSyncTest, ledger_from_header_keeps_the_map_hashes_it_was_given)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    LedgerHeader header;
    header.seq = 2;
    header.txHash = chain.rootHash.asUInt256();
    header.hash = calculateLedgerHash(header);
    auto const verifiedHash = header.hash;

    Ledger ledger{header, noAmendments(), f};
    ASSERT_FALSE(ledger.isImmutable());

    // The map is empty, so it hashes to zero. Both maps are still valid, so settling succeeds.
    ASSERT_TRUE(ledger.txMap().getHash().isZero());
    ASSERT_TRUE(ledger.setImmutable());
    EXPECT_TRUE(ledger.isImmutable());

    EXPECT_EQ(ledger.header().txHash, chain.rootHash.asUInt256());
    EXPECT_EQ(ledger.header().hash, verifiedHash);
}

// Invalid is terminal: neither setImmutable() nor clearSynching() leads back out of it, however
// many times they are called. setSynching() is left out, since an invalid map cannot reach it,
// which its UNREACHABLE states.
TEST_F(SHAMapSyncTest, invalid_state_is_terminal)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));
    ASSERT_TRUE(chain.addOffendingNode(map).isInvalid());
    ASSERT_FALSE(map.isValid());

    // Repeated attempts must each fail, and must not leave the map reporting a valid state.
    for (auto attempt = 0; attempt < 3; ++attempt)
    {
        EXPECT_FALSE(map.setImmutable()) << "attempt " << attempt;
        EXPECT_FALSE(map.isValid()) << "attempt " << attempt;
    }

    // Nor does clearSynching(), which keeps an abandoned map from being moved back to Modifying and
    // passing isValid() again. It refuses rather than treating that as unreachable, since a
    // concurrent walk can invalidate a map between a caller's own check and this call.
    for (auto attempt = 0; attempt < 3; ++attempt)
    {
        map.clearSynching();
        EXPECT_FALSE(map.isValid()) << "attempt " << attempt;
    }

    // isSynching() answers false for an invalid map.
    EXPECT_FALSE(map.isSynching());
}

// Immutable holds against clearSynching(): a walk that ends after the ledger settled leaves the map
// settled rather than reopening it for modification.
TEST_F(SHAMapSyncTest, clear_synching_leaves_an_immutable_map_immutable)
{
    TestNodeFamily f{j_};

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();
    ASSERT_TRUE(map.setImmutable());
    ASSERT_TRUE(map.isImmutable());

    map.clearSynching();

    EXPECT_TRUE(map.isImmutable());
    EXPECT_FALSE(map.isSynching());
}

// A snapshot shares the source map's root, so Invalid carries over to it.
TEST_F(SHAMapSyncTest, snapshot_of_invalid_map_stays_invalid)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));

    auto const result = chain.addOffendingNode(map);
    ASSERT_TRUE(tallyIs(result, 0, 1, 0));
    ASSERT_FALSE(map.isValid());

    // Both flavors: the immutable snapshot is the one the store reads, and the
    // mutable one is the one that sets Modifying.
    for (bool const isMutable : {false, true})
    {
        auto const snapshot = map.snapShot(isMutable);
        ASSERT_TRUE(snapshot != nullptr);
        EXPECT_FALSE(snapshot->isValid()) << "isMutable " << isMutable;
        EXPECT_FALSE(snapshot->setImmutable()) << "isMutable " << isMutable;
    }

    // A snapshot of a sound map is unaffected.
    SHAMap valid{SHAMapType::FREE, f};
    valid.addItem(SHAMapNodeType::TnAccountState, makeRandomAS());
    EXPECT_TRUE(valid.snapShot(false)->isValid());
    EXPECT_TRUE(valid.snapShot(true)->isValid());
}

// getMissingNodes() refuses an invalid map outright, before consulting a filter. addKnownNode()
// reaches the verdict here. A walk reaches it on its own in
// get_missing_nodes_rejects_inner_node_at_leaf_depth.
TEST_F(SHAMapSyncTest, get_missing_nodes_refuses_invalid_map)
{
    // The lookups are counted because the result alone cannot tell the refusal from a walk that
    // reaches the same verdict: the loop discards what it collected once the map is invalid.
    struct CountingFilter : ChainFilter
    {
        using ChainFilter::ChainFilter;

        // How many times the walk asked for a node.
        mutable std::size_t lookups = 0;

        /**
         * Serve a node as ChainFilter does, and count the request.
         *
         * @param hash The hash of the node asked for.
         * @return The node's prefixed form, or nullopt when it is not served.
         */
        [[nodiscard]] std::optional<Blob>
        getNode(SHAMapHash const& hash) const override
        {
            ++lookups;
            return ChainFilter::getNode(hash);
        }
    };

    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    // Unbacked, like TransactionAcquire's map, so the walk resolves synchronously.
    map.setUnbacked();
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));

    auto const offendingResult = chain.addOffendingNode(map);
    ASSERT_TRUE(tallyIs(offendingResult, 0, 1, 0));
    ASSERT_FALSE(map.isValid());

    // Only the node the map rejected, offered back as a fetch pack does.
    CountingFilter const filter{chain, SHAMap::kLeafDepth, SHAMap::kLeafDepth};

    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_EQ(filter.lookups, 0u);
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// A walk reaches kLeafDepth through the filter alone, since each node is checked against its own
// hash and the walk resolves every level locally. The walk is what reaches the verdict here.
TEST_F(SHAMapSyncTest, get_missing_nodes_rejects_inner_node_at_leaf_depth)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    // Unbacked so the walk resolves each level synchronously through the filter.
    map.setUnbacked();
    map.setSynching();

    // Only the root goes in through the sync path. Everything below comes from the filter, so the
    // map is still valid when the walk starts.
    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    ChainFilter const filter{chain};

    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());

    // The walk reaches the same verdict addKnownNode() does, so the map is invalid and
    // setImmutable() refuses.
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());

    // An empty result from a valid map clears the synching flag. An invalid map returns before
    // that, and stays invalid across a second walk.
    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_FALSE(map.isValid());
}

// The depth rule holds on a full-below cache hit reached by a walk too. A node's hash covers its
// child hashes but not its depth, so a hit cannot stand in for the depth check. Backed, as
// InboundLedger's map is.
TEST_F(SHAMapSyncTest, get_missing_nodes_rejects_inner_node_at_leaf_depth_before_the_cache_is_read)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    // Backed, unlike the cases above, so the full-below cache is consulted at all.
    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    // This case seeds the entry a lookup at the boundary would match: the offending node's hash
    // at its own position. The walk skips the lookup there, so the entry is never read and the
    // depth verdict stands. Drop the skip and the hit returns for the whole branch, guard
    // included.
    f.getFullBelowCache()->insert(
        chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256(), chain.idAt(SHAMap::kLeafDepth));

    ChainFilter const filter{chain};

    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// Every read a walk posts is drained before the walk returns, the verdict included: the
// MissingNodes block lives on getMissingNodes()'s stack frame and each posted read holds a
// reference to it. Each level here has an unresolvable second child, so reads are outstanding when
// the verdict lands. The expectations below read the result alone, so a sanitizer build is what
// exercises the drain.
TEST_F(SHAMapSyncTest, get_missing_nodes_drains_posted_reads_when_invalidated)
{
    TestNodeFamily f{j_};
    auto const chain = DeepChain::withDecoys();

    // The decoy is the point of the shape: every inner node has a second child, so the case
    // pins that shape rather than trusting the builder.
    for (unsigned int depth = 0; depth < SHAMap::kLeafDepth; ++depth)
    {
        ASSERT_EQ(
            safeDowncast<SHAMapInnerNode const*>(chain.nodeAt(depth).get())->getBranchCount(), 2);
    }

    // Backed, so descendAsync() posts real asynchronous reads rather than resolving inline.
    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    // Only the chain nodes are served, so the decoy at each level has to be read asynchronously.
    ChainFilter const filter{chain};

    // The walk descends the chain, posting a read per level for the decoy child, and marks the map
    // invalid on reaching kLeafDepth. Returning empty is the visible part; draining first is the
    // part only a sanitizer can see.
    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// The depth rule holds for a node an asynchronous read resolves, and not only for one the walk
// resolves in line: a read carries a hash, which covers a node's contents and not its depth. Only a
// leaf may sit at kLeafDepth, so the inner node offered there is refused and the map is abandoned.
//
// The drain has to be what reaches that verdict. A cap of one node, with a read outstanding for a
// decoy at every level, spends the cap inside the drain, so the walk returns as soon as the drain
// ends. It never reaches the further pass in which gmnProcessNodes would condemn a node the drain
// had hooked in. Dropping the drain's own test therefore leaves a valid map with a node to report,
// and both expectations below refuse that.
TEST_F(SHAMapSyncTest, get_missing_nodes_rejects_inner_node_at_leaf_depth_from_an_async_read)
{
    // A seed of this case's own, so the node it stores answers for no other chain: the memory
    // nodestore is keyed by path, and every test family in this binary opens the same one.
    static constexpr unsigned int kOwnChainSeed = 41;

    // Any value serves: the nodestore is keyed by hash and takes this only as a lookup hint.
    static constexpr std::uint32_t kStoredLedgerSeq = 7;

    TestNodeFamily f{j_};

    // A decoy at every level, so reads are still outstanding when the drain reaches the deepest
    // node and the one-node cap is spent within that same drain.
    auto const chain = DeepChain::withDecoys(kOwnChainSeed);

    // Backed, so descendAsync() posts a real asynchronous read for a child no filter answers for.
    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    // Only the store holds the node at kLeafDepth, so the read for it reaches the database.
    f.db().store(
        NodeObjectType::AccountNode,
        chain.prefixedNodeAt(SHAMap::kLeafDepth),
        chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256(),
        kStoredLedgerSeq);

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    // Served no deeper than the level above kLeafDepth, so the walk descends to there in line and
    // nothing answers for the deepest node synchronously.
    ChainFilter const filter{chain, SHAMap::kLeafDepth - 1};

    // One node, so the cap is spent on a decoy inside the drain and the walk returns straight
    // after it.
    EXPECT_TRUE(map.getMissingNodes(1, &filter).empty());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// A walk that only meets legitimate depths must be left alone. Stopping one level short of
// kLeafDepth leaves a deepest node whose child is genuinely missing, so the walk reports it and
// the map stays valid.
TEST_F(SHAMapSyncTest, get_missing_nodes_accepts_inner_node_above_leaf_depth)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // Every node above kLeafDepth is served, so the one at kLeafDepth is what the walk reports.
    SHAMapHash const withheld = chain.nodeAt(SHAMap::kLeafDepth)->getHash();
    ChainFilter const filter{chain, SHAMap::kLeafDepth - 1};

    auto const missing = map.getMissingNodes(kMaxNodesPerRequest, &filter);

    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0].first.getDepth(), SHAMap::kLeafDepth);
    EXPECT_EQ(missing[0].second, withheld.asUInt256());
    EXPECT_TRUE(map.isValid());
}

// The negative control for the depth verdict: a real leaf at kLeafDepth is what belongs there, so a
// walk that resolves one through the filter completes the map and leaves it valid.
TEST_F(SHAMapSyncTest, get_missing_nodes_accepts_leaf_at_leaf_depth)
{
    TestNodeFamily f{j_};
    auto const chain = DeepChain::toLeaf(SHAMap::kLeafDepth);

    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // Every node is served, the leaf included, so nothing is left to fetch.
    ChainFilter const filter{chain};

    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
    EXPECT_TRUE(map.isValid());
    EXPECT_FALSE(map.isSynching());
    EXPECT_TRUE(map.setImmutable());
}

// The parent-and-branch overload of belongsAt has to agree with the id-taking one on every input. A
// child's id is a prefix of a leaf's key exactly when the parent's id is a prefix of that key and
// the parent's nibble names the branch taken, so both terms are swept: parents on the leaf's path
// and parents naming another subtree, at three depths, over all sixteen branches.
TEST_F(SHAMapSyncTest, belongs_at_by_branch_agrees_with_belongs_at_by_child_id)
{
    // A leaf two levels down, with an inner node above it, so both kinds of node are real.
    auto const chain = DeepChain::toLeaf(2);

    auto const leaf = chain.nodeAt(2);
    ASSERT_TRUE(leaf != nullptr);
    ASSERT_TRUE(leaf->isLeaf());

    auto const inner = chain.nodeAt(1);
    ASSERT_TRUE(inner != nullptr);
    ASSERT_TRUE(inner->isInner());

    // The leaf's own key with its first nibble moved, so a parent built from it below the root
    // names a subtree the leaf does not sit under. selectBranch reads the high nibble of a byte at
    // an even depth, so flipping a bit of byte 0's high nibble moves depth 0 alone.
    UInt256 offPathKey = chain.pathKey;
    offPathKey.begin()[0] = static_cast<unsigned char>(offPathKey.begin()[0] ^ 0x10u);

    for (auto const& parentKey : {chain.pathKey, offPathKey})
    {
        for (auto depth = 0u; depth < 3u; ++depth)
        {
            auto const parentID = SHAMapNodeID::createID(depth, parentKey);

            for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
            {
                EXPECT_EQ(
                    belongsAt(parentID, branch, *leaf),
                    belongsAt(parentID.getChildNodeID(branch), *leaf))
                    << "depth " << depth << " branch " << branch;

                // An inner node carries no key, so every position holds for it.
                EXPECT_TRUE(belongsAt(parentID, branch, *inner))
                    << "depth " << depth << " branch " << branch;
            }
        }
    }

    // Both verdicts are reached, so the sweep above is not agreement between two constant answers.
    // A parent one subtree over refuses the branch a parent on the leaf's path accepts. The root is
    // left out of that second check, since every key shares it.
    for (auto depth = 0u; depth < 3u; ++depth)
    {
        auto const onPath = SHAMapNodeID::createID(depth, chain.pathKey);
        auto const ownBranch = selectBranch(onPath, chain.pathKey);
        EXPECT_TRUE(belongsAt(onPath, ownBranch, *leaf)) << "depth " << depth;

        if (depth > 0u)
        {
            EXPECT_FALSE(belongsAt(SHAMapNodeID::createID(depth, offPathKey), ownBranch, *leaf))
                << "depth " << depth;
        }
    }
}

// The clearSynching() call site in addRootNode() needs a leaf root, and so a zero root hash. An
// invalid map always has an inner root with a non-zero hash, so the root is treated as a duplicate
// and the flag stands.
TEST_F(SHAMapSyncTest, add_root_node_leaves_invalid_map_invalid)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));

    auto const offendingResult = chain.addOffendingNode(map);
    ASSERT_TRUE(tallyIs(offendingResult, 0, 1, 0));
    ASSERT_FALSE(map.isValid());

    // A duplicate: counted as good, and counted in the duplicate tally.
    auto const result = map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr);
    EXPECT_TRUE(tallyIs(result, 0, 0, 1));
    EXPECT_TRUE(result.isGood());
    EXPECT_FALSE(result.isUseful());

    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// The concurrent half of the same contract: a walk writing Invalid while another thread calls
// setImmutable(). trySetState() leaves Invalid in place, so the map ends up invalid and
// setImmutable() refuses.
//
// Meaningful only under ThreadSanitizer. Skipped at run time, so every build still parses the body.
TEST_F(SHAMapSyncTest, invalid_state_survives_concurrent_set_immutable)
{
#ifndef XRPL_TSAN
    GTEST_SKIP() << "Only meaningful under ThreadSanitizer";
#endif

    static constexpr auto kRounds = 200uz;

    for (auto round = 0uz; round < kRounds; ++round)
    {
        TestNodeFamily f{j_};
        DeepChain const chain;

        SHAMap map{SHAMapType::FREE, f};
        map.setUnbacked();
        map.setSynching();

        // Only the root goes in through the sync path, so the map is still valid here. The walk
        // below resolves the rest through the filter.
        ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());
        ChainFilter const filter{chain};

        // One thread walks and invalidates while the other calls setImmutable() repeatedly. Both
        // wait on the same flag, so neither is given a head start.
        std::atomic<bool> go{false};

        std::thread walker([&] {
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            map.getMissingNodes(kMaxNodesPerRequest, &filter);
        });

        std::thread setter([&] {
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (auto attempt = 0uz; attempt < 64uz; ++attempt)
                static_cast<void>(map.setImmutable());
        });

        go.store(true, std::memory_order_release);
        walker.join();
        setter.join();

        // The walk always reaches the verdict, so the map ends up invalid and setImmutable()
        // refuses.
        EXPECT_FALSE(map.isValid()) << "round " << round;
        EXPECT_FALSE(map.setImmutable()) << "round " << round;
    }
}

// A map marked complete in the database withdraws that claim the first time a read misses, and
// reports the miss once so the ledger can be re-acquired. Sixteen unresolvable branches are posted
// in one pass, so with four reader threads finishFetch() runs concurrently for one map. Only a
// ThreadSanitizer build observes the ordering.
TEST_F(SHAMapSyncTest, full_flag_is_withdrawn_once_by_concurrent_readers)
{
    static constexpr auto kRounds = 8uz;
    static constexpr auto kReadThreads = 4;

    for (auto round = 0uz; round < kRounds; ++round)
    {
        TestNodeFamily f{j_, kReadThreads};
        WideRoot const root;

        // Backed, so descendAsync() posts real asynchronous reads rather than resolving inline.
        SHAMap map{SHAMapType::FREE, f};
        map.setSynching();

        ASSERT_TRUE(map.addRootNode(root.hash, root.node, nullptr).isGood());

        // The claim the first miss has to withdraw.
        map.setFull();

        // A null filter, so every branch is read from a database that lacks it.
        EXPECT_EQ(map.getMissingNodes(kMaxNodesPerRequest, nullptr).size(), SHAMap::kBranchFactor)
            << "round " << round;

        EXPECT_EQ(f.missingBySeqReports(), 1uz) << "round " << round;
    }
}

// Every node the sync path hands to a filter carries the map's ledger sequence. All three call
// sites are covered: a root taken from a peer, a node taken from a peer, and a node the walk
// resolved out of the filter itself.
TEST_F(SHAMapSyncTest, sync_filter_is_told_the_ledger_sequence)
{
    static constexpr std::uint32_t kLedgerSeq = 7;

    TestNodeFamily f{j_};

    // A three-level chain, built bottom-up so each hash covers the one below it. Every node keeps
    // its one child on branch 0. The deepest points at a child the fixture withholds, so the walk
    // always has something to ask for.
    auto const deepest = makeCompressedInnerNode({{.branch = 0u, .hash = SHAMapHash{UInt256{1}}}});
    auto const middle = makeCompressedInnerNode({{.branch = 0u, .hash = deepest->getHash()}});
    auto const root = makeCompressedInnerNode({{.branch = 0u, .hash = middle->getHash()}});

    // Unbacked, so the filter is the only source, and a node it withholds counts as missing.
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();
    map.setSynching();
    map.setLedgerSeq(kLedgerSeq);

    RecordingFilter filter;

    ASSERT_TRUE(map.addRootNode(root->getHash(), root, &filter).isGood());
    ASSERT_TRUE(map.addKnownNode(SHAMapNodeID{1, UInt256{}}, middle, &filter).isUseful());

    // The deepest node becomes resolvable only now, after the two above were added directly.
    filter.serve(deepest);
    auto const missing = map.getMissingNodes(kMaxNodesPerRequest, &filter);

    // The walk resolved the deepest node through the filter and then asked for its child.
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0].second, UInt256{1});

    ASSERT_EQ(filter.reports().size(), 3u);

    // The two nodes taken from a peer, which the filter is told about so it can store them.
    EXPECT_FALSE(filter.reports()[0].fromFilter);
    EXPECT_EQ(filter.reports()[0].hash, root->getHash());
    EXPECT_FALSE(filter.reports()[1].fromFilter);
    EXPECT_EQ(filter.reports()[1].hash, middle->getHash());

    // The one the walk read back out of the filter, which is reported as such.
    EXPECT_TRUE(filter.reports()[2].fromFilter);
    EXPECT_EQ(filter.reports()[2].hash, deepest->getHash());

    for (auto const& report : filter.reports())
        EXPECT_EQ(report.ledgerSeq, kLedgerSeq) << "hash " << report.hash;
}

// Ledger::setFull() publishes each map's ledger sequence alongside the flag that lets the first
// nodestore miss report a gap. The sequence is what the lookup resolving that gap reads.
//
// This pins that setFull() sets the sequence. Only a ThreadSanitizer build observes the order of
// the two stores.
TEST_F(SHAMapSyncTest, ledger_set_full_publishes_the_ledger_sequence)
{
    static constexpr std::uint32_t kLedgerSeq = 7;

    TestNodeFamily f{j_};

    LedgerHeader header;
    header.seq = kLedgerSeq;
    // Non-zero, so the map has a root to look for and the lookup can miss.
    header.txHash = UInt256{1};
    header.hash = calculateLedgerHash(header);

    Ledger ledger{header, noAmendments(), f};

    // The constructor already looked for that root and missed. The map becomes complete only at
    // setFull() below, so the report count is still zero here.
    ASSERT_EQ(f.missingBySeqReports(), 0uz);

    ledger.setFull();

    // Still missing, and now the map has a claim to withdraw, so the gap is reported.
    // TestNodeFamily throws in place of the real family's re-acquisition, which finishFetch()
    // logs and swallows.
    EXPECT_FALSE(ledger.txMap().fetchRoot(SHAMapHash{header.txHash}, nullptr));

    EXPECT_EQ(f.missingBySeqReports(), 1uz);
    EXPECT_EQ(f.missingBySeqRefNum(), kLedgerSeq);
}

TEST_F(SHAMapSyncTest, sync)
{
    TestNodeFamily f{j_}, f2{j_};
    SHAMap source{SHAMapType::FREE, f};
    SHAMap destination{SHAMapType::FREE, f2};

    static constexpr auto kItemCount = 10000uz;
    static constexpr auto kInvariantInterval = 100uz;
    static constexpr auto kNodesToConfuse = 500uz;

    for (auto i = 0uz; i < kItemCount; ++i)
    {
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAS());
        if (i % kInvariantInterval == 0)
            source.invariants();
    }

    source.invariants();
    ASSERT_TRUE(confuseMap(source, kNodesToConfuse));
    source.invariants();

    ASSERT_TRUE(source.setImmutable());

    std::size_t count = 0;
    source.visitLeaves([&count]([[maybe_unused]] auto const& item) { ++count; });
    EXPECT_EQ(count, kItemCount);

    std::vector<SHAMapMissingNode> missingNodes;
    source.walkMap(missingNodes, kMaxNodesPerRequest);
    EXPECT_TRUE(missingNodes.empty());

    destination.setSynching();

    {
        std::vector<SHAMapNodeData> a;

        ASSERT_TRUE(source.getNodeFat(SHAMapNodeID(), a, randBool(eng_), randInt(eng_, 2)));

        ASSERT_FALSE(a.empty()) << "NodeSize";

        auto node = SHAMapTreeNode::makeFromWire(makeSlice(a[0].data));
        if (!node)
            FAIL() << "Could not create node";
        ASSERT_TRUE(destination.addRootNode(source.getHash(), std::move(node), nullptr).isGood());
    }

    do
    {
        f.clock().advance(std::chrono::seconds(1));

        // get the list of nodes we know we need
        auto nodesMissing = destination.getMissingNodes(kMaxNodesPerRequest, nullptr);

        if (nodesMissing.empty())
            break;

        // get as many nodes as possible based on this information
        std::vector<SHAMapNodeData> b;

        for (auto& it : nodesMissing)
        {
            // Keep failures fatal here because this loop is data-dependent.
            // non-deterministic number of times and the number of tests run
            // should be deterministic
            if (!source.getNodeFat(it.first, b, randBool(eng_), randInt(eng_, 2)))
                FAIL() << "Unable to fetch node";
        }

        // Keep failures fatal here because this loop is data-dependent.
        // non-deterministic number of times and the number of tests run
        // should be deterministic
        if (b.empty())
            FAIL() << "No nodes returned";

        for (auto const& i : b)
        {
            // Keep failures fatal here because this loop is data-dependent.
            // non-deterministic number of times and the number of tests run
            // should be deterministic
            auto node = SHAMapTreeNode::makeFromWire(makeSlice(i.data));
            if (!node)
                FAIL() << "Could not create node";
            if (i.isLeaf != node->isLeaf())
                FAIL() << "Node is not a leaf";
            if (!destination.addKnownNode(i.nodeID, std::move(node), nullptr).isUseful())
                FAIL() << "Known node was not useful";
        }
    } while (true);

    destination.clearSynching();

    EXPECT_TRUE(source.deepCompare(destination));

    destination.invariants();
}

// The duplicate verdict also answers for a node that did not need to be added, which is the half
// of incDuplicate()'s contract that "already held" does not cover. addKnownNode() reaches it when
// the map has stopped taking nodes, and returns there before looking at the offer at all.
//
// An A/B on one offer and two maps of the same shape, so the synching state is the only difference
// between the two verdicts. Neither map holds the offered node, so a count that meant "already
// held" would be wrong for it.
TEST_F(SHAMapSyncTest, add_known_node_reports_duplicate_once_a_map_stops_synching)
{
    TestNodeFamily f{j_};

    // A well-formed inner node with one child. Its contents do not matter: both verdicts below
    // are reached without the map reading them.
    auto const makeOffer = [] {
        Serializer s;
        s.addBitString(UInt256{1});
        s.add8(0);
        s.add8(kWireTypeCompressedInner);
        return SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    };
    ASSERT_TRUE(makeOffer());

    SHAMapNodeID const target{1, UInt256{}};

    // Synching, so the offer is examined, and refused because the empty root has no branch to
    // hook it onto. The point is that the map looked.
    SHAMap synching{SHAMapType::FREE, f};
    synching.setSynching();

    auto const examined = synching.addKnownNode(target, makeOffer(), nullptr);
    EXPECT_TRUE(examined.isInvalid());
    EXPECT_EQ(examined.getDuplicate(), 0);

    // The same offer to a map that has stopped taking nodes.
    SHAMap stopped{SHAMapType::FREE, f};
    stopped.setSynching();
    stopped.clearSynching();
    ASSERT_FALSE(stopped.isSynching());

    auto const notNeeded = stopped.addKnownNode(target, makeOffer(), nullptr);
    EXPECT_EQ(notNeeded.getGood(), 0);
    EXPECT_EQ(notNeeded.getBad(), 0);
    EXPECT_EQ(notNeeded.getDuplicate(), 1);
    EXPECT_TRUE(notNeeded.isGood()) << "a node that was not needed counts on the accepted side";
}

// `visitDifferences` walks this map and reports the nodes the other map lacks, which is how a fetch
// pack is assembled (see LedgerMaster's populateFetchPack). It decides what to skip through the
// private `hasInnerNode` and `hasLeafNode`, so it is the only route a test has to them. The cases
// below pin both answers: a node reported, and a node skipped.

// `visitDifferences` returns early while the root hash is still zero, so a map built here is sealed
// first. `setImmutable` only moves the state, and `getHash` is what unshares the tree and makes the
// hashes real.
//
// A failed ASSERT_ returns from this helper alone, not from the calling test, so every call site
// wraps it in ASSERT_NO_FATAL_FAILURE.
static void
finalize(SHAMap& map)
{
    ASSERT_TRUE(map.setImmutable());
    ASSERT_FALSE(map.getHash().isZero());
}

TEST_F(SHAMapSyncTest, visit_differences_reports_only_what_is_missing)
{
    TestNodeFamily f{j_};

    // Enough shared items to build inner nodes of their own, so the walk has whole matching
    // subtrees to skip.
    std::vector<boost::intrusive_ptr<SHAMapItem>> shared;
    shared.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        shared.push_back(makeRandomAS());
    }

    auto const extra = makeRandomAS();

    SHAMap have{SHAMapType::FREE, f};
    for (auto const& item : shared)
    {
        ASSERT_TRUE(have.addItem(SHAMapNodeType::TnAccountState, item));
    }
    ASSERT_NO_FATAL_FAILURE(finalize(have));

    SHAMap want{SHAMapType::FREE, f};
    for (auto const& item : shared)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, item));
    }
    ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, extra));
    ASSERT_NO_FATAL_FAILURE(finalize(want));

    std::vector<UInt256> leaves;
    std::size_t inners = 0;
    want.visitDifferences(&have, [&leaves, &inners](SHAMapTreeNode const& node) {
        if (node.isLeaf())
        {
            leaves.push_back(leafKey(node));
        }
        else
        {
            ++inners;
        }
        return true;
    });

    // Every shared leaf is already on the far side, so only `extra` is worth sending.
    EXPECT_EQ(leaves, std::vector<UInt256>{extra->key()});

    // The inner nodes on `extra`'s path are reported and the matching subtrees are skipped, so the
    // walk visits fewer inner nodes than the tree holds.
    std::size_t allInners = 0;
    want.visitNodes([&allInners](SHAMapTreeNode& node) {
        if (!node.isLeaf())
        {
            ++allInners;
        }
        return true;
    });
    EXPECT_GT(inners, 0u);
    EXPECT_LT(inners, allInners);
}

TEST_F(SHAMapSyncTest, visit_differences_against_identical_map_reports_nothing)
{
    TestNodeFamily f{j_};

    auto const item = makeRandomAS();

    SHAMap have{SHAMapType::FREE, f};
    ASSERT_TRUE(have.addItem(SHAMapNodeType::TnAccountState, item));
    ASSERT_NO_FATAL_FAILURE(finalize(have));

    SHAMap want{SHAMapType::FREE, f};
    ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, item));
    ASSERT_NO_FATAL_FAILURE(finalize(want));
    ASSERT_EQ(want.getHash(), have.getHash());

    std::size_t visited = 0;
    want.visitDifferences(&have, [&visited]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++visited;
        return true;
    });

    EXPECT_EQ(visited, 0u);
}

TEST_F(SHAMapSyncTest, visit_differences_against_no_map_reports_every_node)
{
    TestNodeFamily f{j_};

    SHAMap want{SHAMapType::FREE, f};
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, makeRandomAS()));
    }
    ASSERT_NO_FATAL_FAILURE(finalize(want));

    // A null `have` means the far side holds nothing, so every node counts as missing. Compared
    // against visitNodes, which does no such filtering.
    std::size_t differences = 0;
    want.visitDifferences(nullptr, [&differences]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++differences;
        return true;
    });

    std::size_t all = 0;
    want.visitNodes([&all]([[maybe_unused]] SHAMapTreeNode& node) {
        ++all;
        return true;
    });

    EXPECT_GT(differences, 0u);
    EXPECT_EQ(differences, all);
}

TEST_F(SHAMapSyncTest, visit_differences_stops_when_callback_returns_false)
{
    TestNodeFamily f{j_};

    SHAMap want{SHAMapType::FREE, f};
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, makeRandomAS()));
    }
    ASSERT_NO_FATAL_FAILURE(finalize(want));

    // Returning false is how populateFetchPack stops once the pack is full.
    std::size_t visited = 0;
    want.visitDifferences(nullptr, [&visited]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++visited;
        return visited < 3;
    });

    EXPECT_EQ(visited, 3u);
}

}  // namespace xrpl::tests
