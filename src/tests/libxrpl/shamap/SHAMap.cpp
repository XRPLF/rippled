#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/InnerNode.h>
#include <shamap/common.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::tests {

#ifndef __INTELLISENSE__
static_assert(std::is_nothrow_destructible<SHAMap>{});
static_assert(!std::is_default_constructible<SHAMap>{});
static_assert(!std::is_copy_constructible<SHAMap>{});
static_assert(!std::is_copy_assignable<SHAMap>{});
static_assert(!std::is_move_constructible<SHAMap>{});
static_assert(!std::is_move_assignable<SHAMap>{});

static_assert(std::is_nothrow_destructible<SHAMap::ConstIterator>{});
static_assert(std::is_copy_constructible<SHAMap::ConstIterator>{});
static_assert(std::is_copy_assignable<SHAMap::ConstIterator>{});
static_assert(std::is_move_constructible<SHAMap::ConstIterator>{});
static_assert(std::is_move_assignable<SHAMap::ConstIterator>{});

static_assert(std::is_nothrow_destructible<SHAMapItem>{});
static_assert(!std::is_default_constructible<SHAMapItem>{});
static_assert(!std::is_copy_constructible<SHAMapItem>{});

static_assert(std::is_nothrow_destructible<SHAMapNodeID>{});
static_assert(std::is_default_constructible<SHAMapNodeID>{});
static_assert(std::is_copy_constructible<SHAMapNodeID>{});
static_assert(std::is_copy_assignable<SHAMapNodeID>{});
static_assert(std::is_move_constructible<SHAMapNodeID>{});
static_assert(std::is_move_assignable<SHAMapNodeID>{});

static_assert(std::is_nothrow_destructible<SHAMapHash>{});
static_assert(std::is_default_constructible<SHAMapHash>{});
static_assert(std::is_copy_constructible<SHAMapHash>{});
static_assert(std::is_copy_assignable<SHAMapHash>{});
static_assert(std::is_move_constructible<SHAMapHash>{});
static_assert(std::is_move_assignable<SHAMapHash>{});

static_assert(std::is_nothrow_destructible<SHAMapTreeNode>{});
static_assert(!std::is_default_constructible<SHAMapTreeNode>{});
static_assert(!std::is_copy_constructible<SHAMapTreeNode>{});
static_assert(!std::is_copy_assignable<SHAMapTreeNode>{});
static_assert(!std::is_move_constructible<SHAMapTreeNode>{});
static_assert(!std::is_move_assignable<SHAMapTreeNode>{});

static_assert(std::is_nothrow_destructible<SHAMapInnerNode>{});
static_assert(!std::is_default_constructible<SHAMapInnerNode>{});
static_assert(!std::is_copy_constructible<SHAMapInnerNode>{});
static_assert(!std::is_copy_assignable<SHAMapInnerNode>{});
static_assert(!std::is_move_constructible<SHAMapInnerNode>{});
static_assert(!std::is_move_assignable<SHAMapInnerNode>{});

static_assert(std::is_nothrow_destructible<SHAMapLeafNode>{});
static_assert(!std::is_default_constructible<SHAMapLeafNode>{});
static_assert(!std::is_copy_constructible<SHAMapLeafNode>{});
static_assert(!std::is_copy_assignable<SHAMapLeafNode>{});
static_assert(!std::is_move_constructible<SHAMapLeafNode>{});
static_assert(!std::is_move_assignable<SHAMapLeafNode>{});
#endif

inline bool
operator!=(SHAMapItem const& a, SHAMapItem const& b)
{
    return a.key() != b.key();
}

struct SHAMapBackingMode
{
    bool backed;
    std::string_view testName;
};

constexpr SHAMapBackingMode kBackedMode{.backed = true, .testName = "backed"};
constexpr SHAMapBackingMode kUnbackedMode{.backed = false, .testName = "unbacked"};

std::string
shamapBackingModeName(::testing::TestParamInfo<SHAMapBackingMode> const& info)
{
    return std::string{info.param.testName};
}

class SHAMapTest : public ::testing::TestWithParam<SHAMapBackingMode>
{
protected:
    beast::Journal const j_{TestSink::instance()};

    static Buffer
    intToVuc(std::uint8_t v)
    {
        Buffer vuc{32};
        vuc.fill(v);
        return vuc;
    }
};

TEST_P(SHAMapTest, add_traverse_snapshot_build_tear_and_iterate)
{
    auto const testMode = GetParam();
    tests::TestNodeFamily f{j_};

    // kH3 and kH4 differ only in the leaf, same terminal node (level 19)
    constexpr UInt256 kH1("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
    constexpr UInt256 kH2("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
    constexpr UInt256 kH3("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
    constexpr UInt256 kH4("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");

    SHAMap sMap{SHAMapType::FREE, f};
    sMap.invariants();
    if (!testMode.backed)
        sMap.setUnbacked();

    auto i1 = makeShamapitem(kH1, intToVuc(1));
    auto i2 = makeShamapitem(kH2, intToVuc(2));
    auto i3 = makeShamapitem(kH3, intToVuc(3));
    auto i4 = makeShamapitem(kH4, intToVuc(4));

    EXPECT_TRUE(sMap.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(*i2))) << "no add";
    sMap.invariants();
    EXPECT_TRUE(sMap.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(*i1))) << "no add";
    sMap.invariants();

    auto i = sMap.begin();
    auto e = sMap.end();
    EXPECT_FALSE(i == e || (*i != *i1)) << "bad traverse";
    ++i;
    EXPECT_FALSE(i == e || (*i != *i2)) << "bad traverse";
    ++i;
    EXPECT_EQ(i, e) << "bad traverse";
    sMap.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(*i4));
    sMap.invariants();
    sMap.delItem(i2->key());
    sMap.invariants();
    sMap.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(*i3));
    sMap.invariants();
    i = sMap.begin();
    e = sMap.end();
    EXPECT_FALSE(i == e || (*i != *i1)) << "bad traverse";
    ++i;
    EXPECT_FALSE(i == e || (*i != *i3)) << "bad traverse";
    ++i;
    EXPECT_FALSE(i == e || (*i != *i4)) << "bad traverse";
    ++i;
    EXPECT_EQ(i, e) << "bad traverse";

    SHAMapHash const mapHash = sMap.getHash();
    std::shared_ptr<SHAMap> const map2 = sMap.snapShot(false);
    map2->invariants();
    EXPECT_EQ(sMap.getHash(), mapHash) << "bad snapshot";
    EXPECT_EQ(map2->getHash(), mapHash) << "bad snapshot";

    SHAMap::Delta delta;
    ASSERT_TRUE(sMap.compare(*map2, delta, 100));
    EXPECT_TRUE(delta.empty());

    EXPECT_TRUE(sMap.delItem(sMap.begin()->key())) << "bad mod";
    sMap.invariants();
    EXPECT_NE(sMap.getHash(), mapHash) << "bad snapshot";
    EXPECT_EQ(map2->getHash(), mapHash) << "bad snapshot";

    ASSERT_TRUE(sMap.compare(*map2, delta, 100));
    ASSERT_EQ(delta.size(), 1);
    EXPECT_EQ(delta.begin()->first, kH1);
    EXPECT_EQ(delta.begin()->second.first, nullptr);
    ASSERT_NE(delta.begin()->second.second, nullptr);
    EXPECT_EQ(delta.begin()->second.second->key(), kH1);

    sMap.dump();
    {
        constexpr std::array kKeys{
            UInt256{"b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
        };

        constexpr std::array kHashes{
            UInt256{"B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F7F"},
            UInt256{"FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE266"},
            UInt256{"4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B75"},
            UInt256{"7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E07"},
            UInt256{"395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A3E"},
            UInt256{"D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DDE4"},
            UInt256{"76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF94361143615"},
            UInt256{"DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230E5"},
        };

        SHAMap map{SHAMapType::FREE, f};
        if (!testMode.backed)
            map.setUnbacked();

        EXPECT_EQ(map.getHash(), beast::kZero);
        for (std::size_t k = 0; k < kKeys.size(); ++k)
        {
            EXPECT_TRUE(map.addItem(
                SHAMapNodeType::TnTransactionNm,
                makeShamapitem(kKeys[k], intToVuc(static_cast<std::uint8_t>(k)))));
            EXPECT_EQ(map.getHash().asUInt256(), kHashes[k]);
            map.invariants();
        }
        // The decrement is in the body, so the counter never steps below zero.
        // UndefinedBehaviorSanitizer reports that wraparound.
        for (std::size_t k = kKeys.size(); k > 0;)
        {
            --k;
            EXPECT_EQ(map.getHash().asUInt256(), kHashes[k]);
            EXPECT_TRUE(map.delItem(kKeys[k]));
            map.invariants();
        }
        EXPECT_EQ(map.getHash(), beast::kZero);
    }

    {
        constexpr std::array kKeys{
            UInt256{"f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            UInt256{"292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
        };

        tests::TestNodeFamily tf{j_};
        SHAMap map{SHAMapType::FREE, tf};
        if (!testMode.backed)
            map.setUnbacked();
        for (auto const& k : kKeys)
        {
            map.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(k, intToVuc(0)));
            map.invariants();
        }

        auto keyIndex = kKeys.size();
        for (auto const& k : map)
            EXPECT_EQ(k.key(), kKeys[--keyIndex]);
    }
}

INSTANTIATE_TEST_SUITE_P(
    BackingMode,
    SHAMapTest,
    ::testing::Values(kBackedMode, kUnbackedMode),
    shamapBackingModeName);

// Exercises the traversal stacks built by belowHelper. Each stack entry pairs a node with the ID
// naming its position, and every push refuses a leaf whose own key does not lie under the branch it
// was reached through, in Release builds as well as Debug ones.
class SHAMapTraversal : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    // Keys that share a long prefix and then fan out across distinct branches, so the deeper inner
    // nodes have several children and traversal must descend many levels.
    static std::vector<UInt256>
    deepFanOutKeys()
    {
        std::vector<UInt256> keys;
        for (unsigned int branch = 0; branch < SHAMap::kBranchFactor; ++branch)
        {
            // Vary the 6th nibble, keeping the first five identical.
            auto text = std::string("abcde") + "0123456789abcdef"[branch];
            text.append(64 - text.size(), '7');
            keys.emplace_back(std::string_view{text});
        }
        return keys;
    }

    // Keys that share all 63 leading nibbles and fan out only at the last one, so the tree is a
    // chain of single-child inner nodes down to depth 63 with the leaves as siblings at depth 64.
    // This exercises kLeafDepth directly, unlike deepFanOutKeys() above, whose fan-out at the 6th
    // nibble keeps the tree only about 6 levels deep.
    static std::vector<UInt256>
    deepFanOutKeysAtLeafDepth()
    {
        std::vector<UInt256> keys;
        for (unsigned int branch = 0; branch < SHAMap::kBranchFactor; ++branch)
        {
            auto text = std::string(63, 'a') + "0123456789abcdef"[branch];
            keys.emplace_back(std::string_view{text});
        }
        return keys;
    }

    static void
    fillMap(SHAMap& map, std::vector<UInt256> const& keys)
    {
        map.setUnbacked();
        for (auto const& k : keys)
        {
            Buffer vuc{32};
            std::fill_n(vuc.data(), vuc.size(), std::uint8_t{1});
            EXPECT_TRUE(
                map.addItem(SHAMapNodeType::TnAccountState, makeShamapitem(k, std::move(vuc))));
            map.invariants();
        }
    }
};

TEST_F(SHAMapTraversal, forward_iteration_visits_every_key_in_order)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);

    std::ranges::sort(keys);
    std::vector<UInt256> visited;
    for (auto const& item : map)
        visited.push_back(item.key());

    EXPECT_EQ(visited, keys);
}

TEST_F(SHAMapTraversal, upper_bound_walks_the_whole_map)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // upperBound from each key must land on its successor, driving belowHelper across every
    // subtree.
    for (std::size_t k = 0; k + 1 < keys.size(); ++k)
    {
        auto it = map.upperBound(keys[k]);
        ASSERT_NE(it, map.end()) << "no successor for key " << k;
        EXPECT_EQ(it->key(), keys[k + 1]) << "wrong successor for key " << k;
    }
    EXPECT_EQ(map.upperBound(keys.back()), map.end());
}

TEST_F(SHAMapTraversal, lower_bound_walks_the_whole_map)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // lowerBound is the reverse direction: belowHelper descends to the greatest key below a
    // subtree.
    for (std::size_t k = 1; k < keys.size(); ++k)
    {
        auto it = map.lowerBound(keys[k]);
        ASSERT_NE(it, map.end()) << "no predecessor for key " << k;
        EXPECT_EQ(it->key(), keys[k - 1]) << "wrong predecessor for key " << k;
    }
    EXPECT_EQ(map.lowerBound(keys.front()), map.end());
}

TEST_F(SHAMapTraversal, bounds_agree_with_iteration_for_absent_keys)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // Probe keys that are not in the map, so the traversal starts mid-tree rather than at a leaf.
    for (unsigned char const c : {0x00, 0x40, 0x80, 0xc0, 0xff})
    {
        UInt256 probe;
        std::fill_n(probe.begin(), probe.size(), c);

        auto const expectedUpper = std::ranges::upper_bound(keys, probe);
        auto const upper = map.upperBound(probe);
        if (expectedUpper == keys.end())
        {
            EXPECT_EQ(upper, map.end()) << "probe " << static_cast<unsigned>(c);
        }
        else
        {
            ASSERT_NE(upper, map.end()) << "probe " << static_cast<unsigned>(c);
            EXPECT_EQ(upper->key(), *expectedUpper) << "probe " << static_cast<unsigned>(c);
        }

        auto const lowerCount = std::ranges::lower_bound(keys, probe) - keys.begin();
        auto const lower = map.lowerBound(probe);
        if (lowerCount == 0)
        {
            EXPECT_EQ(lower, map.end()) << "probe " << static_cast<unsigned>(c);
        }
        else
        {
            ASSERT_NE(lower, map.end()) << "probe " << static_cast<unsigned>(c);
            EXPECT_EQ(lower->key(), keys[lowerCount - 1]) << "probe " << static_cast<unsigned>(c);
        }
    }

    // Probe keys that land on a leaf and require the leaf-pop-and-resume case. Keys share
    // "abcde" as a prefix and vary the 6th nibble, so a probe that shares the full prefix
    // but differs in the padding hits a leaf from either side: '0' padded with '0' lands below
    // the first key, and 'f' padded with 'f' lands above the last.
    for (char const nibble : {'0', 'f'})
    {
        auto text = std::string("abcde") + nibble;
        text.append(64 - text.size(), nibble);
        UInt256 const probe{std::string_view{text}};

        auto const expectedUpper = std::ranges::upper_bound(keys, probe);
        auto const upper = map.upperBound(probe);
        if (expectedUpper == keys.end())
        {
            EXPECT_EQ(upper, map.end()) << "nibble " << nibble;
        }
        else
        {
            ASSERT_NE(upper, map.end()) << "nibble " << nibble;
            EXPECT_EQ(upper->key(), *expectedUpper) << "nibble " << nibble;
        }

        auto const lowerCount = std::ranges::lower_bound(keys, probe) - keys.begin();
        auto const lower = map.lowerBound(probe);
        if (lowerCount == 0)
        {
            EXPECT_EQ(lower, map.end()) << "nibble " << nibble;
        }
        else
        {
            ASSERT_NE(lower, map.end()) << "nibble " << nibble;
            EXPECT_EQ(lower->key(), keys[lowerCount - 1]) << "nibble " << nibble;
        }
    }
}

TEST_F(SHAMapTraversal, bounds_on_empty_map_return_end)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    // An empty map still leaves its root on the path, so boundHelper reads a non-empty path and
    // answers end() from it.
    EXPECT_EQ(map.upperBound(UInt256{}), map.end());
    EXPECT_EQ(map.lowerBound(UInt256{}), map.end());

    UInt256 probe;
    std::fill_n(probe.begin(), probe.size(), std::uint8_t{0xff});
    EXPECT_EQ(map.upperBound(probe), map.end());
    EXPECT_EQ(map.lowerBound(probe), map.end());
}

TEST_F(SHAMapTraversal, bounds_on_single_item_map_use_the_leaf_below_the_root)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};

    auto const key = deepFanOutKeys().front();
    fillMap(map, {key});

    // fillMap adds items in-process, so root_ stays an inner node with the single leaf below it.
    // The stack holds both, so boundHelper examines the leaf first.
    UInt256 below = key;
    --below;
    UInt256 above = key;
    ++above;

    auto const upper = map.upperBound(below);
    ASSERT_NE(upper, map.end());
    EXPECT_EQ(upper->key(), key);
    EXPECT_EQ(map.upperBound(key), map.end());
    EXPECT_EQ(map.upperBound(above), map.end());

    auto const lower = map.lowerBound(above);
    ASSERT_NE(lower, map.end());
    EXPECT_EQ(lower->key(), key);
    EXPECT_EQ(map.lowerBound(key), map.end());
    EXPECT_EQ(map.lowerBound(below), map.end());
}

TEST_F(SHAMapTraversal, iteration_survives_deletions)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // Deleting every other key drops the fan-out node's branch count from 16 to 8, never the 1
    // that would make delItem collapse it into a leaf. So this pins that iteration survives
    // deletions that reshape the map without collapsing any inner node; the case that does
    // collapse one is iteration_survives_a_collapsed_inner_node below.
    for (std::size_t k = 0; k < keys.size(); k += 2)
    {
        ASSERT_TRUE(map.delItem(keys[k]));
        map.invariants();
    }

    std::vector<UInt256> expected;
    for (std::size_t k = 1; k < keys.size(); k += 2)
        expected.push_back(keys[k]);

    std::vector<UInt256> visited;
    for (auto const& item : map)
        visited.push_back(item.key());
    EXPECT_EQ(visited, expected);

    for (std::size_t k = 0; k + 1 < expected.size(); ++k)
    {
        auto it = map.upperBound(expected[k]);
        ASSERT_NE(it, map.end());
        EXPECT_EQ(it->key(), expected[k + 1]);
    }
}

TEST_F(SHAMapTraversal, iteration_survives_a_collapsed_inner_node)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};

    // One key in a separate subtree, diverging from the fan-out group at the very first nibble, so
    // it survives untouched while the fan-out group below is collapsed.
    auto const sentinel = UInt256{std::string_view{std::string(64, '0')}};

    auto fanOutKeys = deepFanOutKeysAtLeafDepth();
    fillMap(map, fanOutKeys);
    Buffer vuc{32};
    std::fill_n(vuc.data(), vuc.size(), std::uint8_t{1});
    ASSERT_TRUE(
        map.addItem(SHAMapNodeType::TnAccountState, makeShamapitem(sentinel, std::move(vuc))));
    map.invariants();

    std::ranges::sort(fanOutKeys);

    // Delete all but the last fan-out key. The fan-out node's branch count drops to 1 on the final
    // delete, which delItem collapses by pulling the sole remaining leaf up in its place; every
    // ancestor above it has exactly one child by construction, so each of those also drops to
    // branch count 1 and collapses in turn, all the way up to (but not including) the root. That
    // final delete replaces the entire 63-level chain with the root pointing straight at the one
    // remaining leaf, so the surviving traversal stack is rebuilt over a drastically different tree
    // shape, not just missing one inner node.
    for (std::size_t k = 0; k + 1 < fanOutKeys.size(); ++k)
    {
        ASSERT_TRUE(map.delItem(fanOutKeys[k]));
        map.invariants();
    }

    std::vector<UInt256> const expected{sentinel, fanOutKeys.back()};
    std::vector<UInt256> visited;
    for (auto const& item : map)
        visited.push_back(item.key());
    EXPECT_EQ(visited, expected);

    auto it = map.upperBound(sentinel);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->key(), fanOutKeys.back());
    EXPECT_EQ(map.upperBound(fanOutKeys.back()), map.end());
}

// The tests below mirror the ones above but use deepFanOutKeysAtLeafDepth(), whose keys share all
// 63 leading nibbles and fan out only at the last one. That puts the leaves at depth
// SHAMap::kLeafDepth, so these traversals walk a chain of single-child inner nodes all the way down
// and exercise the kLeafDepth guards that deepFanOutKeys() alone (fanning out at the 6th nibble)
// never reaches.

TEST_F(SHAMapTraversal, forward_iteration_visits_every_key_in_order_at_leaf_depth)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeysAtLeafDepth();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);

    std::ranges::sort(keys);
    std::vector<UInt256> visited;
    for (auto const& item : map)
        visited.push_back(item.key());

    EXPECT_EQ(visited, keys);
}

TEST_F(SHAMapTraversal, upper_bound_walks_the_whole_map_at_leaf_depth)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeysAtLeafDepth();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // upperBound from each key must land on its successor, driving belowHelper down to depth
    // kLeafDepth for every subtree.
    for (std::size_t k = 0; k + 1 < keys.size(); ++k)
    {
        auto it = map.upperBound(keys[k]);
        ASSERT_NE(it, map.end()) << "no successor for key " << k;
        EXPECT_EQ(it->key(), keys[k + 1]) << "wrong successor for key " << k;
    }
    EXPECT_EQ(map.upperBound(keys.back()), map.end());
}

TEST_F(SHAMapTraversal, lower_bound_walks_the_whole_map_at_leaf_depth)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeysAtLeafDepth();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // lowerBound is the reverse direction: belowHelper descends to depth kLeafDepth to find the
    // greatest key below a subtree.
    for (std::size_t k = 1; k < keys.size(); ++k)
    {
        auto it = map.lowerBound(keys[k]);
        ASSERT_NE(it, map.end()) << "no predecessor for key " << k;
        EXPECT_EQ(it->key(), keys[k - 1]) << "wrong predecessor for key " << k;
    }
    EXPECT_EQ(map.lowerBound(keys.front()), map.end());
}

TEST_F(SHAMapTraversal, bounds_agree_with_iteration_for_absent_keys_at_leaf_depth)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeysAtLeafDepth();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // The keys fill all 16 branches of the last nibble, so an absent key must diverge from the
    // shared 'a' prefix earlier than that. Diverging at increasingly deep nibbles forces
    // walkTowardsKey to descend through more single-child inner nodes before it finds the empty
    // branch, right up to the one just above kLeafDepth.
    for (unsigned int const divergeAt : {0u, 31u, 61u, 62u})
    {
        // '9' sorts below the shared 'a' prefix and 'b' above it, so the probe lands under or
        // over the whole key block -- driving belowHelper's First and Last descents respectively.
        for (char const nibble : {'9', 'b'})
        {
            auto text = std::string(divergeAt, 'a') + nibble;
            text.append(64 - text.size(), '0');
            UInt256 const probe{std::string_view{text}};

            auto const expectedUpper = std::ranges::upper_bound(keys, probe);
            auto const upper = map.upperBound(probe);
            if (expectedUpper == keys.end())
            {
                EXPECT_EQ(upper, map.end()) << "divergeAt " << divergeAt << " nibble " << nibble;
            }
            else
            {
                ASSERT_NE(upper, map.end()) << "divergeAt " << divergeAt << " nibble " << nibble;
                EXPECT_EQ(upper->key(), *expectedUpper)
                    << "divergeAt " << divergeAt << " nibble " << nibble;
            }

            auto const lowerCount = std::ranges::lower_bound(keys, probe) - keys.begin();
            auto const lower = map.lowerBound(probe);
            if (lowerCount == 0)
            {
                EXPECT_EQ(lower, map.end()) << "divergeAt " << divergeAt << " nibble " << nibble;
            }
            else
            {
                ASSERT_NE(lower, map.end()) << "divergeAt " << divergeAt << " nibble " << nibble;
                EXPECT_EQ(lower->key(), keys[lowerCount - 1])
                    << "divergeAt " << divergeAt << " nibble " << nibble;
            }
        }
    }
}

TEST_F(SHAMapTraversal, iteration_survives_deletions_at_leaf_depth)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeysAtLeafDepth();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);
    std::ranges::sort(keys);

    // Deleting every other key drops the fan-out node's branch count from 16 to 8, the same
    // non-collapsing case as iteration_survives_deletions above, but reached by descending through
    // a chain of single-child inner nodes down to kLeafDepth instead of a shallow one.
    for (std::size_t k = 0; k < keys.size(); k += 2)
    {
        ASSERT_TRUE(map.delItem(keys[k]));
        map.invariants();
    }

    std::vector<UInt256> expected;
    for (std::size_t k = 1; k < keys.size(); k += 2)
        expected.push_back(keys[k]);

    std::vector<UInt256> visited;
    for (auto const& item : map)
        visited.push_back(item.key());
    EXPECT_EQ(visited, expected);

    for (std::size_t k = 0; k + 1 < expected.size(); ++k)
    {
        auto it = map.upperBound(expected[k]);
        ASSERT_NE(it, map.end());
        EXPECT_EQ(it->key(), expected[k + 1]);
    }
}

class SHAMapPathProof : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};
};

TEST_F(SHAMapPathProof, verify_proof_path)
{
    tests::TestNodeFamily tf{j_};
    SHAMap map{SHAMapType::FREE, tf};
    map.setUnbacked();

    UInt256 key;
    UInt256 rootHash;
    std::vector<Blob> goodPath;

    static constexpr unsigned char kFirstKey = 1;
    static constexpr unsigned char kKeyCount = 100;
    static constexpr unsigned char kLastKey = kKeyCount - 1;

    for (unsigned char c = kFirstKey; c < kKeyCount; ++c)
    {
        UInt256 k(c);
        map.addItem(SHAMapNodeType::TnAccountState, makeShamapitem(k, Slice{k.data(), k.size()}));
        map.invariants();

        auto root = map.getHash().asUInt256();
        auto path = map.getProofPath(k);
        if (!path)
        {
            ADD_FAILURE() << "Missing proof path";
            return;
        }
        auto& proofPath = *path;

        EXPECT_TRUE(map.verifyProofPath(root, k, proofPath));
        if (c == kFirstKey)
        {
            // extra node
            proofPath.insert(proofPath.begin(), proofPath.front());
            EXPECT_FALSE(map.verifyProofPath(root, k, proofPath));
            // wrong key
            UInt256 const wrongKey(c + 1);
            EXPECT_FALSE(map.getProofPath(wrongKey));
        }
        if (c == kLastKey)
        {
            key = k;
            rootHash = root;
            goodPath = std::move(proofPath);
        }
    }

    // still good
    EXPECT_TRUE(map.verifyProofPath(rootHash, key, goodPath));
    // empty path
    std::vector<Blob> badPath;
    EXPECT_FALSE(map.verifyProofPath(rootHash, key, badPath));
    // too long
    badPath = goodPath;
    badPath.push_back(goodPath.back());
    EXPECT_FALSE(map.verifyProofPath(rootHash, key, badPath));
    // bad node
    badPath.clear();
    badPath.emplace_back(100, 100);
    EXPECT_FALSE(map.verifyProofPath(rootHash, key, badPath));
    // bad node type
    badPath.clear();
    badPath.push_back(goodPath.front());
    badPath.front().back()--;  // change node type
    EXPECT_FALSE(map.verifyProofPath(rootHash, key, badPath));
    // all inner
    badPath.clear();
    badPath = goodPath;
    badPath.erase(badPath.begin());
    EXPECT_FALSE(map.verifyProofPath(rootHash, key, badPath));
}

// A legitimate proof path for two keys sharing all 63 leading nibbles is 65 elements: inner nodes
// at depths 0..63 plus the leaf at depth 64. This pins that the 65 bound is real, so the fix for
// the built-path case below must not simply tighten the length limit.
TEST_F(SHAMapPathProof, legitimate_deep_path_is_sixty_five_elements)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    auto const kA = UInt256{std::string_view{std::string(63, 'a') + "1"}};
    auto const kB = UInt256{std::string_view{std::string(63, 'a') + "2"}};

    for (auto const& k : {kA, kB})
    {
        Buffer vuc{32};
        std::fill_n(vuc.data(), vuc.size(), std::uint8_t{1});
        ASSERT_TRUE(map.addItem(SHAMapNodeType::TnAccountState, makeShamapitem(k, std::move(vuc))));
    }
    map.invariants();

    auto const pathA = map.getProofPath(kA);
    ASSERT_TRUE(pathA.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) has_value() checked above
    EXPECT_EQ(pathA->size(), 65u);
    EXPECT_TRUE(SHAMap::verifyProofPath(map.getHash().asUInt256(), kA, *pathA));
    // NOLINTEND(bugprone-unchecked-optional-access)

    auto const pathB = map.getProofPath(kB);
    ASSERT_TRUE(pathB.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) has_value() checked above
    EXPECT_EQ(pathB->size(), 65u);
    EXPECT_TRUE(SHAMap::verifyProofPath(map.getHash().asUInt256(), kB, *pathB));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// A built path of 65 hash-chained inner nodes reaches depth kLeafDepth, where only the leaf
// terminating the path may sit. Such a path must be rejected.
TEST_F(SHAMapPathProof, all_inner_path_at_leaf_depth_is_rejected)
{
    // An arbitrary well-formed key; the test does not care about its specific value.
    constexpr UInt256 kTestKey("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

    // Build upwards from the deepest node so each parent's selected branch carries its child's hash
    // and the hash chain validates at every level.
    std::vector<Blob> path;
    SHAMapHash childHash{UInt256{1}};

    // The decrement is in the body, so the counter never steps below zero.
    // UndefinedBehaviorSanitizer reports that wraparound.
    for (auto depth = SHAMap::kLeafDepth + 1u; depth > 0;)
    {
        --depth;
        auto const id = SHAMapNodeID::createID(std::min(depth, SHAMap::kLeafDepth - 1u), kTestKey);
        auto const branch = selectBranch(id, kTestKey);

        Serializer s;
        for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
            s.addBitString(i == branch ? childHash.asUInt256() : UInt256{});
        s.add8(kWireTypeInner);
        path.push_back(s.getData());

        auto node = SHAMapTreeNode::makeFromWire(makeSlice(path.back()));
        ASSERT_TRUE(node);
        node->updateHash();
        childHash = node->getHash();
    }

    ASSERT_EQ(path.size(), 65u);
    EXPECT_FALSE(SHAMap::verifyProofPath(childHash.asUInt256(), kTestKey, path));
}

/**
 * Wrap a leaf blob in a forged root inner node whose branch for `key` carries that leaf's hash.
 *
 * The resulting two-element path hash-chains for `key` no matter which leaf sits at the bottom,
 * which is exactly the substitution a peer could attempt.
 *
 * @param leafBlob the wire form of the leaf to place at the bottom of the path.
 * @param key the key the forged path claims to prove.
 * @return the path (deepest element first) and the forged root hash, or an empty path if the leaf
 *         blob does not parse.
 */
static std::pair<std::vector<Blob>, UInt256>
forgeRootOverLeaf(Blob const& leafBlob, UInt256 const& key)
{
    auto leaf = SHAMapTreeNode::makeFromWire(makeSlice(leafBlob));
    if (!leaf || !leaf->isLeaf())
        return {};
    leaf->updateHash();

    auto const branch = selectBranch(SHAMapNodeID::createID(0, key), key);
    Serializer s;
    for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
        s.addBitString(i == branch ? leaf->getHash().asUInt256() : UInt256{});
    s.add8(kWireTypeInner);

    auto root = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    if (!root)
        return {};
    root->updateHash();

    return {std::vector<Blob>{leafBlob, s.getData()}, root->getHash().asUInt256()};
}

// The hash chain above a leaf proves nothing about which key that leaf holds, so a peer can graft a
// genuine leaf from elsewhere in the map onto a path built for another key. Comparing the terminal
// leaf's own key against the key being proved is what rejects it.
TEST_F(SHAMapPathProof, substituted_leaf_for_other_key_is_rejected)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    // Two arbitrary keys differing in their first nibble, so each leaf hangs off the root directly.
    constexpr UInt256 kKey("1c8cec8e5e9b0e5e0e0f5b3e2c9f7a1d6b4e8c2a0d7f3b9e5c1a8d4f2b6e0c93");
    constexpr UInt256 kOtherKey("e3f1a7d5b9c2e8f406a1d3b5c7e9f2a4d6b8c0e2f4a6d8b0c2e4f6a8d0b2c4e6");

    for (auto const& k : {kKey, kOtherKey})
    {
        ASSERT_TRUE(map.addItem(
            SHAMapNodeType::TnAccountState, makeShamapitem(k, Slice{k.data(), k.size()})));
    }
    map.invariants();

    auto const ownPath = map.getProofPath(kKey);
    auto const otherPath = map.getProofPath(kOtherKey);
    ASSERT_TRUE(ownPath.has_value());
    ASSERT_TRUE(otherPath.has_value());

    // NOLINTBEGIN(bugprone-unchecked-optional-access) has_value() checked above
    // The genuine leaf blobs, deepest element first.
    auto const& ownLeaf = ownPath->front();
    auto const& otherLeaf = otherPath->front();
    // NOLINTEND(bugprone-unchecked-optional-access)

    // Control: the forged root is accepted when the leaf below it really is kKey's leaf, so the
    // rejection below can only come from the leaf key comparison.
    auto const [goodPath, goodRoot] = forgeRootOverLeaf(ownLeaf, kKey);
    ASSERT_EQ(goodPath.size(), 2u);
    EXPECT_TRUE(SHAMap::verifyProofPath(goodRoot, kKey, goodPath));

    // Same forged root, but kOtherKey's leaf substituted at the bottom: the hash chain still
    // validates, yet the path does not prove anything about kKey.
    auto const [badPath, badRoot] = forgeRootOverLeaf(otherLeaf, kKey);
    ASSERT_EQ(badPath.size(), 2u);
    EXPECT_FALSE(SHAMap::verifyProofPath(badRoot, kKey, badPath));
}

/**
 * Run `call` and check it throws SHAMapMissingNode naming a key, not a hash.
 *
 * A walk that decides it cannot go on names the key it was searching for, while
 * a failure to fetch names the hash. Both throw the same type, so the payload
 * is what tells the two apart.
 *
 * @param call the call expected to throw.
 */
template <class Call>
void
expectThrowNamingKey(Call&& call)
{
    try
    {
        call();
        ADD_FAILURE() << "expected SHAMapMissingNode, nothing was thrown";
    }
    catch (SHAMapMissingNode const& e)
    {
        std::string const what{e.what()};
        EXPECT_NE(what.find(": id "), std::string::npos) << what;
        EXPECT_EQ(what.find(": hash "), std::string::npos) << what;
    }
}

/**
 * A filter that resolves a fixed set of nodes, by hash.
 *
 * Stands in for the real sync filters, which serve a node from a local cache
 * keyed on its hash, so the hash is all they check.
 */
class FixedNodeFilter : public SHAMapSyncFilter
{
    std::map<SHAMapHash, Blob> nodes_;

public:
    FixedNodeFilter(SHAMapHash const& hash, Blob blob)
    {
        nodes_.emplace(hash, std::move(blob));
    }

    explicit FixedNodeFilter(std::vector<std::pair<SHAMapHash, Blob>> nodes)
    {
        for (auto& [hash, blob] : nodes)
            nodes_.emplace(hash, std::move(blob));
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
};

// A tree whose hashes all agree can still put a leaf where its key does not belong, because a hash
// covers a node's contents rather than its position.
class SHAMapMisplacedLeaf : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    // An arbitrary key whose first nibble is 1, so its leaf belongs under branch 1 of the root.
    static constexpr UInt256 kKey{
        "1c8cec8e5e9b0e5e0e0f5b3e2c9f7a1d6b4e8c2a0d7f3b9e5c1a8d4f2b6e0c93"};

    // Any branch other than the one kKey selects at depth 0.
    static constexpr unsigned int kWrongBranch = 5;

    // The sequence a test writes stored nodes under, and sets on the map that reads them back. Any
    // value serves: the nodestore is keyed by hash and takes this only as a lookup hint.
    static constexpr std::uint32_t kStoredLedgerSeq = 11;

    /**
     * A genuine leaf holding kKey, in the form a sync filter serves, with its
     * hash.
     *
     * Taken from a map that placed the leaf correctly, so only its position is
     * ever wrong below. Serialized with its prefix rather than in wire form,
     * since that is what checkFilter parses.
     *
     * @param f the family the throwaway source map belongs to.
     * @return the leaf's prefixed form and its hash, or an empty blob if the
     *         map rejected the item.
     */
    static std::pair<Blob, SHAMapHash>
    genuineLeaf(Family& f)
    {
        SHAMap source{SHAMapType::FREE, f};
        source.setUnbacked();
        if (!source.addItem(
                SHAMapNodeType::TnAccountState,
                makeShamapitem(kKey, Slice{kKey.data(), kKey.size()})))
        {
            return {};
        }

        auto const path = source.getProofPath(kKey);
        if (!path.has_value() || path->empty())
            return {};

        auto leaf = SHAMapTreeNode::makeFromWire(makeSlice(path->front()));
        if (!leaf || !leaf->isLeaf())
            return {};
        leaf->updateHash();

        Serializer s;
        leaf->serializeWithPrefix(s);
        return {s.getData(), leaf->getHash()};
    }

    /**
     * Assemble `map` as a root inner node holding the given children.
     *
     * The root is installed directly, as a peer's would be, so each child stays
     * unresolved until a walk consults the filter for it. Takes a list rather
     * than one hash because a walk only reaches the sideways scans in
     * peekNextItem and boundHelper from a branch it can already resolve, which
     * needs a second, well-placed child.
     *
     * @param map the map to assemble, which must be synching and empty.
     * @param children the branch and hash of each child the forged root
     *        records.
     * @return whether the root was accepted.
     */
    static bool
    forgeRoot(SHAMap& map, std::vector<InnerChild> const& children)
    {
        auto root = makeFullInnerNode(children);
        if (!root)
            return false;

        auto const rootHash = root->getHash();
        return map.addRootNode(rootHash, std::move(root), nullptr).isGood();
    }

    /**
     * A correctly-built leaf for the given key, in the form a sync filter
     * serves.
     *
     * Serialized with its prefix rather than in wire form, since that is what
     * checkFilter parses.
     *
     * @param f the family the source map belongs to.
     * @param key the key to build the leaf for.
     * @return the leaf's prefixed form and its hash, or an empty blob if the
     *         map rejected the item.
     */
    static std::pair<Blob, SHAMapHash>
    leafFor(Family& f, UInt256 const& key)
    {
        SHAMap source{SHAMapType::FREE, f};
        source.setUnbacked();
        if (!source.addItem(
                SHAMapNodeType::TnAccountState, makeShamapitem(key, Slice{key.data(), key.size()})))
        {
            return {};
        }

        auto const path = source.getProofPath(key);
        if (!path.has_value() || path->empty())
            return {};

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) has_value() checked above
        auto const leaf = SHAMapTreeNode::makeFromWire(makeSlice(path->front()));
        if (!leaf || !leaf->isLeaf())
            return {};

        Serializer s;
        leaf->serializeWithPrefix(s);
        return {s.getData(), leaf->getHash()};
    }

    /**
     * A key whose first nibble equals its last.
     *
     * Any key builds a chain. This one also lets a refusal be told apart from
     * an accident: a walk that ignored the depth bound selects, at the level
     * past kLeafDepth, the one branch the node there occupies, and so reports
     * a child it could not fetch instead of answering.
     */
    static constexpr UInt256 kChainKeyPastLeafDepth{
        "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6cab"};

    /**
     * Store a chain of single-child inner nodes running one level past
     * kLeafDepth.
     *
     * Built bottom-up, so each parent records the hash of the node below it and
     * the chain verifies at every level. The deepest node points at a child the
     * store does not hold.
     *
     * @param f the family whose store the chain is written to.
     * @param key the key whose nibbles choose the branch at every level.
     * @return the chain's root and the hash of the node at kLeafDepth, or an
     *         empty pair if a node could not be built.
     */
    static std::pair<SHAMapTreeNodePtr, SHAMapHash>
    storeInnerChainPastLeafDepth(tests::TestNodeFamily& f, UInt256 const& key)
    {
        SHAMapHash childHash{UInt256{1}};
        SHAMapTreeNodePtr node;
        SHAMapHash deepestHash;

        // The decrement is in the body, so the counter never steps below zero.
        // UndefinedBehaviorSanitizer reports that wraparound.
        for (auto depth = SHAMap::kLeafDepth + 1u; depth > 0;)
        {
            --depth;
            auto const id = SHAMapNodeID::createID(std::min(depth, SHAMap::kLeafDepth - 1u), key);
            node = makeFullInnerNode({{.branch = selectBranch(id, key), .hash = childHash}});
            if (!node)
                return {};

            Serializer prefixed;
            node->serializeWithPrefix(prefixed);
            f.db().store(
                NodeObjectType::AccountNode,
                prefixed.getData(),
                node->getHash().asUInt256(),
                kStoredLedgerSeq);

            childHash = node->getHash();
            if (depth == SHAMap::kLeafDepth)
                deepestHash = childHash;
        }

        return {std::move(node), deepestHash};
    }
};

// Every branch above a leaf is judged, not only the last one, so a leaf under a subtree recorded on
// a branch its key does not select is refused.
TEST_F(SHAMapMisplacedLeaf, iterating_a_misplaced_subtree_throws)
{
    // A key greater than the probe below, so a greater key exists for that probe to find.
    constexpr UInt256 kDeepKey{"a100000000000000000000000000000000000000000000000000000000000000"};

    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = leafFor(sourceFamily, kDeepKey);
    ASSERT_FALSE(leafBlob.empty());

    // The subtree holds exactly one child, on the branch its key selects at depth 1. One occupied
    // branch is reached the same way whichever branch the scan starts from, so which node the walk
    // resolves does not depend on the random start.
    //
    // Assembled here rather than taken from a source map, because a one-item map puts its leaf
    // directly under the root and so holds no inner node to borrow.
    auto const leafBranch = selectBranch(SHAMapNodeID::createID(1, kDeepKey), kDeepKey);
    auto const inner = makeFullInnerNode({{.branch = leafBranch, .hash = leafHash}});
    ASSERT_TRUE(inner);

    Serializer innerPrefixed;
    inner->serializeWithPrefix(innerPrefixed);

    // The leaf is served as well, so the walk reaches it and the throw below is about position.
    std::vector<std::pair<SHAMapHash, Blob>> served;
    served.emplace_back(inner->getHash(), innerPrefixed.getData());
    served.emplace_back(leafHash, leafBlob);

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, {{.branch = kWrongBranch, .hash = inner->getHash()}}));

    // The inner node carries no key, so the leaf below it is what shows the branch it was reached
    // through disagrees with the key underneath.
    FixedNodeFilter const filter{std::move(served)};
    map.getMissingNodes(4, &filter);

    EXPECT_THROW(map.begin(), SHAMapMissingNode);

    // The bounds refuse the same map, and a refusal throws where end() answers. The probe selects
    // kWrongBranch at depth 0 and leafBranch at depth 1, so the descent walks straight into the
    // misplaced leaf and clears the path. The one key in the map is greater than the probe.
    UInt256 probe;
    probe.begin()[0] = static_cast<std::uint8_t>((kWrongBranch << 4) | leafBranch);
    ASSERT_GT(kDeepKey, probe);

    EXPECT_THROW(map.upperBound(probe), SHAMapMissingNode);
    EXPECT_THROW(map.lowerBound(probe), SHAMapMissingNode);
}

// peekNextItem scans the branches beyond the one a key takes and judges each node it resolves
// there. The root holds a well-placed leaf so the scan starts, and a misplaced one for it to reach.
TEST_F(SHAMapMisplacedLeaf, iterating_past_a_leaf_into_a_misplaced_sibling_throws)
{
    // First nibble 10, so this leaf sits where it belongs. kKey's first nibble is 1, so its leaf
    // does not belong on kStrayBranch, which is above 10 and so within the forward scan's reach.
    constexpr UInt256 kPlacedKey{
        "a000000000000000000000000000000000000000000000000000000000000000"};
    static constexpr unsigned int kStrayBranch = 12;

    tests::TestNodeFamily sourceFamily{j_};
    auto const [placedBlob, placedHash] = leafFor(sourceFamily, kPlacedKey);
    ASSERT_FALSE(placedBlob.empty());
    auto const [strayBlob, strayHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(strayBlob.empty());

    // Both leaves go in the store rather than a filter, because the scan reaches them through
    // descendThrow(), which consults no filter. Served from the store, the throw is about
    // position.
    tests::TestNodeFamily targetFamily{j_};
    targetFamily.db().store(
        NodeObjectType::AccountNode, Blob{placedBlob}, placedHash.asUInt256(), kStoredLedgerSeq);
    targetFamily.db().store(
        NodeObjectType::AccountNode, Blob{strayBlob}, strayHash.asUInt256(), kStoredLedgerSeq);

    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setLedgerSeq(kStoredLedgerSeq);

    auto const placedBranch = selectBranch(SHAMapNodeID{}, kPlacedKey);
    ASSERT_LT(placedBranch, kStrayBranch);
    ASSERT_TRUE(forgeRoot(
        map,
        {{.branch = placedBranch, .hash = placedHash},
         {.branch = kStrayBranch, .hash = strayHash}}));

    // The store really holds both, so a refusal below is about position.
    ASSERT_NE(targetFamily.db().fetchNodeObject(placedHash.asUInt256(), kStoredLedgerSeq), nullptr);
    ASSERT_NE(targetFamily.db().fetchNodeObject(strayHash.asUInt256(), kStoredLedgerSeq), nullptr);

    // The well-placed leaf resolves, so iteration starts.
    auto it = map.begin();
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->key(), kPlacedKey);

    // Advancing scans past placedBranch, resolves the stray leaf out of the store, and pushChild
    // refuses it. The payload check narrows this to peekNextItem's two id-form throws, of which
    // only the refused push can occur against this fixture.
    expectThrowNamingKey([&] { ++it; });
}

// boundHelper runs the same sideways scan as peekNextItem and has its own throw for a node it
// cannot push. Reached the same way, from a probe whose own branch resolves.
TEST_F(SHAMapMisplacedLeaf, a_bound_scanning_into_a_misplaced_sibling_throws)
{
    constexpr UInt256 kPlacedKey{
        "a000000000000000000000000000000000000000000000000000000000000000"};
    static constexpr unsigned int kStrayBranch = 12;

    tests::TestNodeFamily sourceFamily{j_};
    auto const [placedBlob, placedHash] = leafFor(sourceFamily, kPlacedKey);
    ASSERT_FALSE(placedBlob.empty());
    auto const [strayBlob, strayHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(strayBlob.empty());

    tests::TestNodeFamily targetFamily{j_};
    targetFamily.db().store(
        NodeObjectType::AccountNode, Blob{placedBlob}, placedHash.asUInt256(), kStoredLedgerSeq);
    targetFamily.db().store(
        NodeObjectType::AccountNode, Blob{strayBlob}, strayHash.asUInt256(), kStoredLedgerSeq);

    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setLedgerSeq(kStoredLedgerSeq);

    auto const placedBranch = selectBranch(SHAMapNodeID{}, kPlacedKey);
    ASSERT_LT(placedBranch, kStrayBranch);
    ASSERT_TRUE(forgeRoot(
        map,
        {{.branch = placedBranch, .hash = placedHash},
         {.branch = kStrayBranch, .hash = strayHash}}));

    ASSERT_NE(targetFamily.db().fetchNodeObject(placedHash.asUInt256(), kStoredLedgerSeq), nullptr);
    ASSERT_NE(targetFamily.db().fetchNodeObject(strayHash.asUInt256(), kStoredLedgerSeq), nullptr);

    // upperBound from the well-placed key descends to its leaf, then scans above it and reaches the
    // misplaced one, whose own key is greater than the probe. Narrowed to boundHelper's two id-form
    // throws, of which only the refused push can occur against this fixture.
    expectThrowNamingKey([&] { static_cast<void>(map.upperBound(kPlacedKey)); });
}

// walkTowardsKey refuses a node past kLeafDepth in both its modes. The mode that records a path for
// its caller refuses through pushChild and clears the path, so the caller reads an empty path as a
// refusal.
TEST_F(SHAMapMisplacedLeaf, a_keyed_walk_recording_a_path_refuses_a_node_past_leaf_depth)
{
    tests::TestNodeFamily targetFamily{j_};
    auto [root, deepestHash] = storeInnerChainPastLeafDepth(targetFamily, kChainKeyPastLeafDepth);
    ASSERT_TRUE(root);
    ASSERT_TRUE(deepestHash.isNonZero());

    // The node the walk has to refuse is really in the store, so the refusal is about depth.
    ASSERT_NE(
        targetFamily.db().fetchNodeObject(deepestHash.asUInt256(), kStoredLedgerSeq), nullptr);

    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setLedgerSeq(kStoredLedgerSeq);
    auto const rootHash = root->getHash();
    ASSERT_TRUE(map.addRootNode(rootHash, std::move(root), nullptr).isGood());

    // delItem hands walkTowardsKey a path to fill, reads the cleared path as a refusal, and names
    // the key it was walking to.
    expectThrowNamingKey([&] { static_cast<void>(map.delItem(kChainKeyPastLeafDepth)); });
}

// The other mode: a walk with no path at all tracks a depth of its own and refuses at the same
// bound, answering rather than throwing.
TEST_F(SHAMapMisplacedLeaf, a_keyed_walk_with_no_path_refuses_a_node_past_leaf_depth)
{
    tests::TestNodeFamily targetFamily{j_};
    auto [root, deepestHash] = storeInnerChainPastLeafDepth(targetFamily, kChainKeyPastLeafDepth);
    ASSERT_TRUE(root);
    ASSERT_TRUE(deepestHash.isNonZero());

    ASSERT_NE(
        targetFamily.db().fetchNodeObject(deepestHash.asUInt256(), kStoredLedgerSeq), nullptr);

    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setLedgerSeq(kStoredLedgerSeq);
    auto const rootHash = root->getHash();
    ASSERT_TRUE(map.addRootNode(rootHash, std::move(root), nullptr).isGood());

    // hasItem reaches findKey, which walks with no path. The walk reports the refusal and stops, so
    // the key reads as absent.
    bool found = true;
    ASSERT_NO_THROW(found = map.hasItem(kChainKeyPastLeafDepth));
    EXPECT_FALSE(found);
}

// A path of inner nodes running past kLeafDepth has no room for the leaf it leads to, so the
// traversal refuses it. The chain is resolvable, so the throw is about depth.
TEST_F(SHAMapMisplacedLeaf, a_path_of_inner_nodes_past_leaf_depth_is_refused)
{
    // An arbitrary key. Only the branches it selects matter here.
    constexpr UInt256 kChainKey{"b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"};

    tests::TestNodeFamily targetFamily{j_};

    // Built bottom-up, so each parent records the hash of the node below it and the chain verifies
    // at every level. The deepest node points at a child the store does not hold.
    SHAMapHash childHash{UInt256{1}};
    SHAMapTreeNodePtr node;

    // The node at kLeafDepth, which is the one the traversal has to refuse. Recorded so the refusal
    // can be told apart from a failure to fetch the sentinel the chain ends on.
    SHAMapHash deepestHash;

    // The decrement is in the body, so the counter never steps below zero.
    // UndefinedBehaviorSanitizer reports that wraparound.
    for (auto depth = SHAMap::kLeafDepth + 1u; depth > 0;)
    {
        --depth;
        auto const id = SHAMapNodeID::createID(std::min(depth, SHAMap::kLeafDepth - 1u), kChainKey);
        node = makeFullInnerNode({{.branch = selectBranch(id, kChainKey), .hash = childHash}});
        ASSERT_TRUE(node);

        Serializer prefixed;
        node->serializeWithPrefix(prefixed);
        targetFamily.db().store(
            NodeObjectType::AccountNode,
            prefixed.getData(),
            node->getHash().asUInt256(),
            kStoredLedgerSeq);

        childHash = node->getHash();
        if (depth == SHAMap::kLeafDepth)
            deepestHash = childHash;
    }
    ASSERT_TRUE(deepestHash.isNonZero());

    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setLedgerSeq(kStoredLedgerSeq);
    ASSERT_TRUE(map.addRootNode(childHash, std::move(node), nullptr).isGood());

    // The chain is really readable, so the refusal below is about depth.
    ASSERT_NE(targetFamily.db().fetchNodeObject(childHash.asUInt256(), kStoredLedgerSeq), nullptr);

    // Refused with SHAMapMissingNode, which is pushChild's other refusal shape: too deep rather
    // than misplaced. The depth bound applies before getChildNodeID is asked for a child.
    try
    {
        static_cast<void>(map.begin());
        ADD_FAILURE() << "the traversal did not refuse the path";
    }
    catch (SHAMapMissingNode const& e)
    {
        // belowHelper's refusal reports the hash of the child it refused, where the scan sites
        // above report the key they were searching for. So the node named here is the one at
        // kLeafDepth.
        std::string const what{e.what()};
        EXPECT_NE(what.find(to_string(deepestHash)), std::string::npos) << what;
    }
}

// addKnownNode reaches a filter through the synchronous descend on its way to the position it was
// given, which is the other route a node takes into a tree during acquisition.
//
// What this pins is the verdict addKnownNode reports and that the map ends invalid.
TEST_F(SHAMapMisplacedLeaf, hooking_a_known_node_invalidates_the_map)
{
    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(leafBlob.empty());

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, {{.branch = kWrongBranch, .hash = leafHash}}));
    ASSERT_TRUE(map.isValid());

    // A key whose first nibble is kWrongBranch, so the walk descends the branch holding the leaf.
    // An inner node is offered rather than a leaf, so the walk reaches the descent and judges
    // what it resolves there.
    auto const target = SHAMapNodeID::createID(
        2, UInt256{"5000000000000000000000000000000000000000000000000000000000000000"});

    Serializer s;
    for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
        s.addBitString(i == 0u ? UInt256{1} : UInt256{});
    s.add8(kWireTypeInner);
    auto offered = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    ASSERT_TRUE(offered);
    offered->updateHash();

    FixedNodeFilter const filter{leafHash, leafBlob};
    auto const result = map.addKnownNode(target, std::move(offered), &filter);

    EXPECT_FALSE(map.isValid());

    // The verdict matters as much as the state: it is what the acquisition paths charge a peer on,
    // so a later change to it should fail here rather than pass quietly.
    EXPECT_TRUE(result.isInvalid());
    EXPECT_FALSE(result.isGood());
}

// The filter descent judges depth as well as position, and this case reaches the depth arm. It
// serves an inner node, for which belongsAt holds by definition, so the verdict below can only come
// from pastLeafDepth. Removing that conjunct from descend() makes this case fail.
//
// The whole chain is served through the filter and nothing is written to the store, because the
// memory nodestore is shared by path across every test family in this binary.
TEST_F(SHAMapMisplacedLeaf, a_filter_serving_an_inner_node_at_leaf_depth_invalidates_the_map)
{
    // A key of this case's own, so the chain it builds is its own too.
    constexpr UInt256 kOwnChainKey{
        "3f0e1d2c3b4a59687766554433221100ffeeddccbbaa99887766554433221100"};

    // Built bottom-up, so each parent records the hash of the node below it. Every node goes to the
    // filter, including the one at kLeafDepth, which is the one the descent has to refuse.
    std::vector<std::pair<SHAMapHash, Blob>> served;
    SHAMapHash childHash{UInt256{1}};
    SHAMapTreeNodePtr node;

    // The decrement is in the body, so the counter never steps below zero.
    // UndefinedBehaviorSanitizer reports that wraparound.
    for (auto depth = SHAMap::kLeafDepth + 1u; depth > 0;)
    {
        --depth;
        auto const id =
            SHAMapNodeID::createID(std::min(depth, SHAMap::kLeafDepth - 1u), kOwnChainKey);
        node = makeFullInnerNode({{.branch = selectBranch(id, kOwnChainKey), .hash = childHash}});
        ASSERT_TRUE(node);
        ASSERT_TRUE(node->isInner());

        Serializer prefixed;
        node->serializeWithPrefix(prefixed);
        served.emplace_back(node->getHash(), prefixed.getData());

        childHash = node->getHash();
    }
    ASSERT_EQ(served.size(), SHAMap::kLeafDepth + 1u);

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(map.addRootNode(childHash, std::move(node), nullptr).isGood());
    ASSERT_TRUE(map.isValid());

    // kLeafDepth is the deepest ID the tree allows, so the walk descends from kLeafDepth - 1 once
    // more and consults the filter for a child at kLeafDepth.
    auto const target = SHAMapNodeID::createID(SHAMap::kLeafDepth, kOwnChainKey);

    Serializer offeredWire;
    for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
        offeredWire.addBitString(i == 0u ? UInt256{1} : UInt256{});
    offeredWire.add8(kWireTypeInner);
    auto offered = SHAMapTreeNode::makeFromWire(makeSlice(offeredWire.peekData()));
    ASSERT_TRUE(offered);
    offered->updateHash();

    FixedNodeFilter const filter{std::move(served)};
    static_cast<void>(map.addKnownNode(target, std::move(offered), &filter));

    EXPECT_FALSE(map.isValid());
}

// addKnownNode also hooks the very node it was handed, on the path where the local store has
// nothing to resolve for that slot. Such a node's position is known only from the ID the caller
// supplied, so it is judged against the leaf's own key before it is hooked.
TEST_F(SHAMapMisplacedLeaf, hooking_an_offered_misplaced_leaf_invalidates_the_map)
{
    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(leafBlob.empty());

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, UInt256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, {{.branch = kWrongBranch, .hash = leafHash}}));
    ASSERT_TRUE(map.isValid());

    // The branch the forged root files the leaf under, which is not the one kKey selects.
    UInt256 wrongPrefix;
    wrongPrefix.begin()[0] = static_cast<std::uint8_t>(kWrongBranch << 4);
    auto const target = SHAMapNodeID::createID(1, wrongPrefix);

    auto offered = SHAMapTreeNode::makeFromPrefix(makeSlice(leafBlob), leafHash);
    ASSERT_TRUE(offered);
    ASSERT_TRUE(offered->isLeaf());

    // No filter, so the walk resolves nothing locally and the node offered here is the one hooked.
    auto const result = map.addKnownNode(target, std::move(offered), nullptr);

    EXPECT_FALSE(map.isValid());
    EXPECT_TRUE(result.isInvalid());
    EXPECT_FALSE(result.isGood());

    // The verdict names which arm refused, not just that one did. This arm condemned the map, so
    // it reports mapInvalidated(); the arm that only rejects a misplaced label reports the plain
    // invalid() and leaves the map usable, which
    // SHAMapSyncTest.only_the_map_invalidating_arm_reports_it holds the other side of. The two
    // decide which fee InboundLedger::receiveNode() charges, so the names cannot be swapped.
    EXPECT_TRUE(result.invalidatedMap());
}
}  // namespace xrpl::tests
