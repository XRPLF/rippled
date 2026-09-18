#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
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
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/DeepChain.h>
#include <shamap/common.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::tests {

// The cap on how many nodes a walk reports, set well above what any test here expects.
static constexpr int kMaxNodesPerRequest = 2048;

/**
 * Rules with no amendments enabled, which is all a ledger built here needs.
 *
 * @return The rules.
 */
[[nodiscard]] static Rules
noAmendments()
{
    return Rules{std::unordered_set<uint256, beast::Uhash<>>{}};
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
    beast::xor_shift_engine eng_;

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

        std::list<uint256> items;

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
     * A single-child inner node in wire form, pointing at the given child.
     *
     * The chain a caller builds from these is put together bottom-up,
     * since each node's hash covers the child hash below it.
     *
     * @param childHash What the node points at, on branch 0.
     * @return The deserialized node.
     */
    [[nodiscard]] static SHAMapTreeNodePtr
    makeInnerNode(SHAMapHash const& childHash)
    {
        Serializer s;
        s.addBitString(childHash.asUInt256());
        s.add8(0);  // the chain continues at branch 0
        s.add8(kWireTypeCompressedInner);

        return SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    }

    /**
     * A sync filter that records every node it is told about, and serves back
     * only the ones it was explicitly asked to hold.
     *
     * Serving is opt-in: the sync path consults the filter before deciding
     * a node is missing, so a filter that served everything it had seen
     * would resolve the node a test is about to offer.
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
         * Offer a node back to the map, the way a fetch pack would.
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
            Serializer s;

            for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
            {
                // Derived from the branch so each posts its own read, and deterministic so it
                // cannot collide with a real node hash.
                uint256 childHash;
                childHash.begin()[0] = 0xFA;
                childHash.begin()[1] = 0xB1;
                childHash.begin()[2] = static_cast<unsigned char>(branch);
                s.addBitString(childHash);
            }

            s.add8(kWireTypeInner);

            node = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
            hash = node->getHash();
        }
    };
};

// An inner node at kLeafDepth, where only leaves can live, leaves the map provably invalid. It
// must be reported as bad data, and the map must then refuse to become immutable.
TEST_F(SHAMapSyncTest, innerNodeAtLeafDepth)
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

    // Invalid is terminal, so the map can no longer be made immutable and therefore cannot be
    // persisted.
    EXPECT_FALSE(map.setImmutable());
}

// A node that cannot be hooked anywhere is bad data, so the batch counts no progress, but the map
// itself is unharmed and another sender can still complete it. All three ways of getting there are
// covered, since they share that verdict.
TEST_F(SHAMapSyncTest, nodeThatCannotBeHookedIsBadData)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(map.addRootNode(chain.rootHash, chain.nodeAt(0), nullptr).isGood());

    // nodeAt(1) is the node the root is missing and its hash matches, but we claim depth 2.
    auto const wrongDepth = map.addKnownNode(SHAMapNodeID{2, uint256{}}, chain.nodeAt(1), nullptr);

    EXPECT_TRUE(tallyIs(wrongDepth, 0, 1, 0));
    EXPECT_FALSE(wrongDepth.isUseful());

    // The chain sits on branch 0 at every depth, so a node claiming a position on branch 1 asks the
    // descent to follow a branch the root does not have.
    uint256 otherBranch;
    otherBranch.begin()[0] = 0x10;
    auto const emptyBranch =
        map.addKnownNode(SHAMapNodeID{1, otherBranch}, chain.nodeAt(1), nullptr);

    EXPECT_TRUE(tallyIs(emptyBranch, 0, 1, 0));

    // The right position this time, but the data hashes to something other than the child the root
    // says belongs there.
    auto const corrupt = map.addKnownNode(SHAMapNodeID{1, uint256{}}, chain.nodeAt(2), nullptr);

    EXPECT_TRUE(tallyIs(corrupt, 0, 1, 0));

    // Nothing was hooked in and nothing was proven about the tree, so the map stays usable.
    EXPECT_TRUE(map.isValid());
}

// The verdict must not be bypassable through the full-below cache. That cache is keyed by node hash
// and shared by every map of a family, and a hash covers a node's children but not its depth, so an
// earlier walk can mark the same subtree hash complete at one depth while this map reaches it at
// kLeafDepth, with no collision involved. A hit there would report a duplicate and return before
// the verdict, leaving what every later caller relies on dependent on what an unrelated map cached.
TEST_F(SHAMapSyncTest, mapInvalidatingNodeIsJudgedOnCacheHit)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));
    ASSERT_TRUE(map.isValid());

    // The cache is keyed on the hash of the child being considered, so at the kLeafDepth boundary
    // that is the offending node itself, and a hit is what would skip the whole branch. This test
    // seeds that entry, unlike the cases above, which never touch the cache and so always miss.
    f.getFullBelowCache()->insert(chain.nodeAt(SHAMap::kLeafDepth)->getHash().asUInt256());

    auto const result = chain.addOffendingNode(map);

    EXPECT_TRUE(tallyIs(result, 0, 1, 0));
    EXPECT_FALSE(result.isGood());
    EXPECT_FALSE(map.isValid());
    EXPECT_FALSE(map.setImmutable());
}

// An invalid tx map must also stop the enclosing ledger from being marked immutable, since an
// immutable ledger is treated as persistable.
TEST_F(SHAMapSyncTest, invalidTxMapBlocksImmutableLedger)
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

// The same for the state map. Ledger::setImmutable() tests both maps in one expression, so this
// covers the second operand: a sound transaction map must not let an invalid state map through.
TEST_F(SHAMapSyncTest, invalidStateMapBlocksImmutableLedger)
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

// A refusal must leave the header exactly as it was. setImmutable() derives the map hashes from the
// maps and then the ledger hash from the header, so writes made before the refusal would relabel
// the ledger on its way to failing, leaving a header that no longer describes what it was built
// from. This case is caught by the check made up front; the same must hold of the re-test after the
// maps are settled, which is why the header is written only once that one has passed too.
TEST_F(SHAMapSyncTest, refusedSettleLeavesTheHeaderAlone)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    // Not the header constructor: this one derives its map hashes, which is what must not happen.
    Ledger ledger{1, NetClock::time_point{}, noAmendments(), Fees{}, f};
    ASSERT_FALSE(ledger.isImmutable());
    ASSERT_TRUE(ledger.header().txHash.isZero());
    ASSERT_TRUE(ledger.header().accountHash.isZero());
    auto const hashBefore = ledger.header().hash;

    // A transaction map that hashes to something, so a derived header hash would differ from the
    // one the ledger has now.
    ASSERT_TRUE(ledger.txMap().addItem(SHAMapNodeType::TnTransactionNm, makeRandomAS()));
    ASSERT_TRUE(ledger.txMap().getHash().isNonZero());

    // And a state map the chain abandons, so settling has to refuse.
    ledger.stateMap().setSynching();
    ASSERT_TRUE(chain.fill(ledger.stateMap()));
    ASSERT_TRUE(chain.addOffendingNode(ledger.stateMap()).isInvalid());
    ASSERT_FALSE(ledger.stateMap().isValid());

    EXPECT_FALSE(ledger.setImmutable());

    // Nothing was written: not the map hashes, not the ledger hash, and not the flag.
    EXPECT_FALSE(ledger.isImmutable());
    EXPECT_TRUE(ledger.header().txHash.isZero());
    EXPECT_TRUE(ledger.header().accountHash.isZero());
    EXPECT_EQ(ledger.header().hash, hashBefore);
}

// Invalid is terminal: setImmutable() and clearSynching() offer no way back out of it, however
// many times they are called. setSynching() is left alone, since it is unreachable on an invalid
// map today and says so with an UNREACHABLE that aborts under -Dassert.
TEST_F(SHAMapSyncTest, invalidStateIsTerminal)
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
    // passing isValid() again. It refuses rather than aborting, since a concurrent walk can
    // invalidate a map between a caller's own check and this call.
    for (auto attempt = 0; attempt < 3; ++attempt)
    {
        map.clearSynching();
        EXPECT_FALSE(map.isValid()) << "attempt " << attempt;
    }

    // An invalid map is not synching either, so nothing reads it as mid-acquisition.
    EXPECT_FALSE(map.isSynching());
}

// A snapshot shares the source map's root, so it inherits whatever the source was found to be.
// Invalid carries over, since promoting it would hand back a map that passes isValid().
TEST_F(SHAMapSyncTest, snapshotOfInvalidMapStaysInvalid)
{
    TestNodeFamily f{j_};
    DeepChain const chain;

    SHAMap map{SHAMapType::FREE, f};
    map.setSynching();

    ASSERT_TRUE(chain.fill(map));

    auto const result = chain.addOffendingNode(map);
    ASSERT_TRUE(tallyIs(result, 0, 1, 0));
    ASSERT_FALSE(map.isValid());

    // Both flavors: the immutable snapshot is the one that would be persisted,
    // and the mutable one would otherwise launder the state back to Modifying.
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

// A map marked complete in the database withdraws that claim the first time a read misses, and
// reports the miss once so the ledger can be re-acquired. Sixteen unresolvable branches are posted
// in one pass, so with four reader threads the misses overlap and finishFetch() runs concurrently
// for a single map. The one-report count below is too narrow a window for an ordinary run to
// police; what confirms it is the absence of a data race under ThreadSanitizer.
TEST_F(SHAMapSyncTest, fullFlagIsWithdrawnOnceByConcurrentReaders)
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

        // No filter, so every branch has to be read from a database that does not hold it.
        EXPECT_EQ(map.getMissingNodes(kMaxNodesPerRequest, nullptr).size(), SHAMap::kBranchFactor)
            << "round " << round;

        EXPECT_EQ(f.missingBySeqReports(), 1uz) << "round " << round;
    }
}

// Every node the sync path hands to a filter carries the map's ledger sequence, which is the hint
// the filter passes on to a nodestore keyed by hash. All three call sites are covered: a root taken
// from a peer, a node taken from a peer, and a node the walk resolved out of the filter itself.
TEST_F(SHAMapSyncTest, syncFilterIsToldTheLedgerSequence)
{
    static constexpr std::uint32_t kLedgerSeq = 7;

    TestNodeFamily f{j_};

    // A three-level chain, built bottom-up so each hash covers the one below it. The deepest node
    // points at a child that is never supplied, so the walk always has something to ask for.
    auto const deepest = makeInnerNode(SHAMapHash{uint256{1}});
    auto const middle = makeInnerNode(deepest->getHash());
    auto const root = makeInnerNode(middle->getHash());

    // Unbacked, so a node the filter does not hold is missing rather than looked up in a database.
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();
    map.setSynching();
    map.setLedgerSeq(kLedgerSeq);

    RecordingFilter filter;

    ASSERT_TRUE(map.addRootNode(root->getHash(), root, &filter).isGood());
    ASSERT_TRUE(map.addKnownNode(SHAMapNodeID{1, uint256{}}, middle, &filter).isUseful());

    // Only now is the deepest node resolvable, so nothing above it came out of the filter.
    filter.serve(deepest);
    auto const missing = map.getMissingNodes(kMaxNodesPerRequest, &filter);

    // The walk resolved the deepest node through the filter and then asked for its child.
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0].second, uint256{1});

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

// Ledger::setFull() has to publish each map's ledger sequence alongside the flag that lets the
// first nodestore miss report a gap, or the report names the zero a map starts with and the lookup
// it asks for cannot resolve.
TEST_F(SHAMapSyncTest, ledgerSetFullPublishesTheLedgerSequence)
{
    static constexpr std::uint32_t kLedgerSeq = 7;

    TestNodeFamily f{j_};

    LedgerHeader header;
    header.seq = kLedgerSeq;
    // Non-zero, so the map has a root to look for and the lookup can miss.
    header.txHash = uint256{1};
    header.hash = calculateLedgerHash(header);

    Ledger ledger{header, noAmendments(), f};

    // The constructor already looked for that root and did not find it, but nothing had claimed the
    // map was complete yet, so there was no gap to report.
    ASSERT_EQ(f.missingBySeqReports(), 0uz);

    ledger.setFull();

    // Still missing, and now the map has a claim to withdraw, so the gap is reported.
    // TestNodeFamily throws where the real family would start re-acquiring, which finishFetch()
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

}  // namespace xrpl::tests
