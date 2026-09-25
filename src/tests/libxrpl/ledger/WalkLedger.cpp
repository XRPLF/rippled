#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>  // SHAMapType
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <gtest/gtest.h>
#include <helpers/TestFamily.h>
#include <helpers/TestSink.h>

#include <cstdint>
#include <unordered_set>
#include <utility>

namespace xrpl::tests {

namespace {

// Fixed seed, so a failure reproduces when the test runs on its own.
constexpr std::uint32_t kSeed = 0x5eed1234U;

/**
 * Build a SHAMapItem holding random data.
 *
 * @param engine The random engine to draw the data from.
 * @return The new item.
 */
boost::intrusive_ptr<SHAMapItem>
makeRandomItem(beast::xor_shift_engine& engine)
{
    static constexpr auto kWordsPerItem = 3;

    Serializer s;
    for (auto word = 0; word < kWordsPerItem; ++word)
        s.add32(randInt<std::uint32_t>(engine));
    return makeShamapitem(s.getSHA512Half(), s.slice());
}

/**
 * Write a map's nodes into a family's node store.
 *
 * @param map The map to read. It must be complete.
 * @param family The family whose store receives the nodes.
 * @param rootOnly Write only the root node, leaving every node below it absent.
 */
void
storeMap(SHAMap const& map, test::TestFamily& family, bool rootOnly)
{
    // visitNodes reports the root first, so rootOnly lets that first call through and
    // stops at the one after it.
    int stored = 0;
    map.visitNodes([&family, rootOnly, &stored](SHAMapTreeNode& node) {
        if (rootOnly && stored > 0)
            return false;

        Serializer s;
        node.serializeWithPrefix(s);
        family.db().store(
            NodeObjectType::AccountNode, std::move(s.modData()), node.getHash().asUInt256(), 0);
        ++stored;
        return true;
    });
}

}  // namespace

// walkLedger must not report a ledger whose transaction map is missing a node as
// complete. The parallel path used to return the state map's result directly and never
// walk the transaction map at all, so Application::loadOldLedger accepted such a ledger.
// Both paths must agree, so this checks the serial one on the same ledger.
TEST(WalkLedger, parallel_walk_checks_the_transaction_map)
{
    static constexpr auto kItems = 200;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    test::TestFamily source{j};
    test::TestFamily dest{j};

    // Two complete maps, built in the source family.
    SHAMap stateMap{SHAMapType::STATE, source};
    SHAMap txMap{SHAMapType::TRANSACTION, source};
    for (auto i = 0; i < kItems; ++i)
    {
        stateMap.addItem(SHAMapNodeType::TnAccountState, makeRandomItem(engine));
        txMap.addItem(SHAMapNodeType::TnTransactionNm, makeRandomItem(engine));
    }
    stateMap.setImmutable();
    txMap.setImmutable();

    // Read both hashes before storing anything. getHash() is what computes the node
    // hashes, through unshare(), so storing first would write every node under a stale
    // hash and nothing would be findable afterwards.
    LedgerHeader header;
    header.seq = 1;
    header.accountHash = stateMap.getHash().asUInt256();
    header.txHash = txMap.getHash().asUInt256();
    header.hash = calculateLedgerHash(header);

    // The destination family gets the whole state map, so the state walk succeeds, and
    // only the transaction map's root, so every node below it is unreadable. The first
    // unreadable node makes SHAMap::finishFetch call TestFamily::missingNodeAcquireBySeq,
    // which throws, so expect one "finishFetch exception" warning in the log below.
    storeMap(stateMap, dest, false);
    storeMap(txMap, dest, true);

    bool loaded = false;
    Ledger const ledger{
        header,
        loaded,
        false,
        Rules{std::unordered_set<uint256, beast::Uhash<>>{}},
        Fees{},
        dest,
        j};

    // Both roots are readable, so the ledger loads.
    ASSERT_TRUE(loaded);

    EXPECT_FALSE(ledger.walkLedger(j, true));
    EXPECT_FALSE(ledger.walkLedger(j, false));
}

// A ledger whose maps are both complete must pass, on the parallel path too.
TEST(WalkLedger, parallel_walk_accepts_a_complete_ledger)
{
    static constexpr auto kItems = 200;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    test::TestFamily source{j};
    test::TestFamily dest{j};

    SHAMap stateMap{SHAMapType::STATE, source};
    SHAMap txMap{SHAMapType::TRANSACTION, source};
    for (auto i = 0; i < kItems; ++i)
    {
        stateMap.addItem(SHAMapNodeType::TnAccountState, makeRandomItem(engine));
        txMap.addItem(SHAMapNodeType::TnTransactionNm, makeRandomItem(engine));
    }
    stateMap.setImmutable();
    txMap.setImmutable();

    // getHash() before storeMap: see the note in the test above.
    LedgerHeader header;
    header.seq = 1;
    header.accountHash = stateMap.getHash().asUInt256();
    header.txHash = txMap.getHash().asUInt256();
    header.hash = calculateLedgerHash(header);

    storeMap(stateMap, dest, false);
    storeMap(txMap, dest, false);

    bool loaded = false;
    Ledger const ledger{
        header,
        loaded,
        false,
        Rules{std::unordered_set<uint256, beast::Uhash<>>{}},
        Fees{},
        dest,
        j};
    ASSERT_TRUE(loaded);

    EXPECT_TRUE(ledger.walkLedger(j, true));
    EXPECT_TRUE(ledger.walkLedger(j, false));
}

}  // namespace xrpl::tests
