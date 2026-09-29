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
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
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

    // This case seeds the entry a lookup at the boundary would match: the offending node's own
    // hash. The descent skips the lookup there, so the entry is never read and the depth verdict
    // stands. Drop the skip and the hit returns for the whole branch, so the tally below becomes
    // a duplicate.
    f.getFullBelowCache()->insert(chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256());

    auto const result = chain.addOffendingNode(map);

    EXPECT_TRUE(tallyIs(result, 0, 1, 0));
    EXPECT_FALSE(result.isGood());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// An invalid tx map must also stop the enclosing ledger from being marked immutable, since an
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

// Invalid is terminal: setImmutable() and clearSynching() offer no way back out of it, however
// many times they are called. setSynching() is left alone, since it is unreachable on an invalid
// map today and says so with an UNREACHABLE.
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
    ChainFilter const filter{chain, SHAMap::kLeafDepth, SHAMap::kLeafDepth};

    EXPECT_TRUE(map.getMissingNodes(kMaxNodesPerRequest, &filter).empty());
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

    // This case seeds the entry a lookup at the boundary would match: the offending node's own
    // hash. The walk skips the lookup there, so the entry is never read and the depth verdict
    // stands. Drop the skip and the hit returns for the whole branch, guard included.
    f.getFullBelowCache()->insert(chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256());

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
// in one pass, so with four reader threads finishFetch() runs concurrently for one map. The nightly
// ThreadSanitizer job covers the ordering.
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
// This pins that setFull() sets the sequence. The nightly ThreadSanitizer job that PR 8245
// adds covers the order of the two stores.
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

}  // namespace xrpl::tests
