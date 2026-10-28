#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <shamap/common.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace xrpl::test {

namespace {

/**
 * A header for the loading constructor, with the given map roots.
 *
 * @param txHash The transaction map root. Zero means no transactions.
 * @param accountHash The state map root. Zero means an empty state.
 * @return The header, with its hash filled in.
 */
LedgerHeader
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
 * Write a one-item map of the given type to the family's store, so a header
 * can name a root that fetchRoot() finds.
 *
 * @param family The family whose store receives the map's nodes.
 * @param type The map type to build. The item and node object types follow it.
 * @return The root hash of the stored map, nonzero.
 */
UInt256
storeOneItemMap(tests::TestNodeFamily& family, SHAMapType type)
{
    bool const isState = type == SHAMapType::STATE;

    // The smallest payload a leaf accepts.
    std::vector<std::uint8_t> const data(kMinShaMapItemBytes, 1);

    SHAMap map{type, family};
    EXPECT_TRUE(map.addItem(
        isState ? SHAMapNodeType::TnAccountState : SHAMapNodeType::TnTransactionNm,
        makeShamapitem(UInt256{7}, Slice{data.data(), data.size()})));
    map.flushDirty(isState ? NodeObjectType::AccountNode : NodeObjectType::TransactionNode);

    return map.getHash().asUInt256();
}

/**
 * Check that the ledger and both of its maps report the given frozen state.
 *
 * @param ledger The ledger to check.
 * @param frozen What isImmutable() is expected to report, for the ledger and
 *        for each of its maps.
 */
void
expectFrozen(Ledger const& ledger, bool frozen)
{
    EXPECT_EQ(ledger.isImmutable(), frozen);
    EXPECT_EQ(ledger.txMap().isImmutable(), frozen);
    EXPECT_EQ(ledger.stateMap().isImmutable(), frozen);
}

}  // namespace

/**
 * The loading constructor reports a ledger whose roots are absent from the
 * store as not loaded, and leaves that ledger and its maps unfrozen.
 */
TEST(LedgerLoad, missing_roots_leave_the_ledger_mutable)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    // Both roots named, and neither in the store.
    auto const header = makeHeader(UInt256{1}, UInt256{2});

    bool loaded = true;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_FALSE(loaded);
    expectFrozen(ledger, false);
}

/**
 * A named transaction root that is absent from the store is enough on its
 * own to report the ledger as not loaded and leave it unfrozen, even when
 * the state root is zero and so has nothing to fetch.
 */
TEST(LedgerLoad, missing_transaction_root_alone_leaves_the_ledger_mutable)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const header = makeHeader(UInt256{1}, UInt256{});

    bool loaded = true;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_FALSE(loaded);
    expectFrozen(ledger, false);
}

/**
 * A named state root that is absent from the store is enough on its own to
 * report the ledger as not loaded and leave it unfrozen, even when the
 * transaction root is zero and so has nothing to fetch.
 */
TEST(LedgerLoad, missing_state_root_alone_leaves_the_ledger_mutable)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const header = makeHeader(UInt256{}, UInt256{2});

    bool loaded = true;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_FALSE(loaded);
    expectFrozen(ledger, false);
}

/**
 * When the store holds the state root but not the transaction root, the
 * ledger is reported as not loaded, and the fetched state map stays unfrozen
 * along with the rest.
 */
TEST(LedgerLoad, missing_transaction_root_beside_a_stored_state_root_leaves_the_ledger_mutable)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const accountHash = storeOneItemMap(family, SHAMapType::STATE);
    ASSERT_TRUE(accountHash.isNonZero());
    auto const header = makeHeader(UInt256{1}, accountHash);

    bool loaded = true;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_FALSE(loaded);
    expectFrozen(ledger, false);
}

/**
 * When the store holds the transaction root but not the state root, the
 * ledger is reported as not loaded, and the fetched transaction map stays
 * unfrozen along with the rest. The mirror of the case above, since each
 * root is fetched by its own branch.
 */
TEST(LedgerLoad, missing_state_root_beside_a_stored_transaction_root_leaves_the_ledger_mutable)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const txHash = storeOneItemMap(family, SHAMapType::TRANSACTION);
    ASSERT_TRUE(txHash.isNonZero());
    auto const header = makeHeader(txHash, UInt256{2});

    bool loaded = true;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_FALSE(loaded);
    expectFrozen(ledger, false);
}

/**
 * A header whose two roots are both in the store loads, and the ledger and
 * both maps are frozen. The positive control for the cases above.
 */
TEST(LedgerLoad, stored_roots_load_and_freeze)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const txHash = storeOneItemMap(family, SHAMapType::TRANSACTION);
    auto const accountHash = storeOneItemMap(family, SHAMapType::STATE);
    ASSERT_TRUE(txHash.isNonZero());
    ASSERT_TRUE(accountHash.isNonZero());
    auto const header = makeHeader(txHash, accountHash);

    bool loaded = false;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_TRUE(loaded);
    expectFrozen(ledger, true);
}

/**
 * A header naming no roots has nothing to fetch, so the ledger loads and is
 * frozen, which is the path a ledger found in the store takes.
 */
TEST(LedgerLoad, zero_roots_load_and_freeze)
{
    tests::TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    std::unordered_set<UInt256, beast::Uhash<>> const presets;

    auto const header = makeHeader(UInt256{}, UInt256{});

    bool loaded = false;
    Ledger const ledger{
        header, loaded, /*acquire=*/false, Rules{presets}, Fees{}, family, family.journal()};

    EXPECT_TRUE(loaded);
    expectFrozen(ledger, true);
}

}  // namespace xrpl::test
