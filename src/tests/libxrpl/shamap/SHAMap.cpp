#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
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
    constexpr uint256 kH1("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
    constexpr uint256 kH2("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
    constexpr uint256 kH3("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
    constexpr uint256 kH4("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");

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
            uint256{"b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
        };

        constexpr std::array kHashes{
            uint256{"B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F7F"},
            uint256{"FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE266"},
            uint256{"4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B75"},
            uint256{"7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E07"},
            uint256{"395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A3E"},
            uint256{"D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DDE4"},
            uint256{"76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF94361143615"},
            uint256{"DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230E5"},
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
        for (std::size_t k = kKeys.size(); k-- > 0;)
        {
            EXPECT_EQ(map.getHash().asUInt256(), kHashes[k]);
            EXPECT_TRUE(map.delItem(kKeys[k]));
            map.invariants();
        }
        EXPECT_EQ(map.getHash(), beast::kZero);
    }

    {
        constexpr std::array kKeys{
            uint256{"f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
            uint256{"292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8"},
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

// Exercises the traversal paths built by belowHelper. A path names each node's position by its own
// length, and every push refuses a leaf whose key does not lie under the branch it was reached
// through, in Release builds as well as Debug ones.
class SHAMapTraversal : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    // Keys that share a long prefix and then fan out across distinct branches, so the deeper inner
    // nodes have several children and traversal must descend many levels.
    static std::vector<uint256>
    deepFanOutKeys()
    {
        std::vector<uint256> keys;
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
    static std::vector<uint256>
    deepFanOutKeysAtLeafDepth()
    {
        std::vector<uint256> keys;
        for (unsigned int branch = 0; branch < SHAMap::kBranchFactor; ++branch)
        {
            auto text = std::string(63, 'a') + "0123456789abcdef"[branch];
            keys.emplace_back(std::string_view{text});
        }
        return keys;
    }

    static void
    fillMap(SHAMap& map, std::vector<uint256> const& keys)
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
    std::vector<uint256> visited;
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
        uint256 probe;
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
        uint256 const probe{std::string_view{text}};

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

TEST_F(SHAMapTraversal, update_give_item_on_absent_key_returns_false)
{
    tests::TestNodeFamily f{j_};
    auto keys = deepFanOutKeys();
    SHAMap map{SHAMapType::FREE, f};
    fillMap(map, keys);

    // Absent key: walkTowardsKey stops on an inner node with an empty branch, not a leaf, so
    // updateGiveItem has to answer for a top of the wrong type. The public API permits the call,
    // so it returns false rather than reporting UNREACHABLE and aborting an instrumented build.
    auto const absentKey = uint256{std::string_view{std::string(64, '0')}};
    Buffer vuc{32};
    std::fill_n(vuc.data(), vuc.size(), std::uint8_t{2});
    EXPECT_FALSE(map.updateGiveItem(
        SHAMapNodeType::TnAccountState, makeShamapitem(absentKey, std::move(vuc))));

    // The other shape an absent key takes: a single-item map queried with a key that selects the
    // same root branch. The walk ends on the leaf it reached, whose key is not the one asked for,
    // so the top is a leaf that does not hold the tag. Both shapes answer false. The difference
    // only shows in an instrumented build, where reporting UNREACHABLE would abort.
    SHAMap single{SHAMapType::FREE, f};
    fillMap(single, {keys.front()});

    auto probe = keys.front();
    std::fill_n(probe.begin() + 1, probe.size() - 1, std::uint8_t{0});
    ASSERT_NE(probe, keys.front());

    Buffer other{32};
    std::fill_n(other.data(), other.size(), std::uint8_t{3});
    EXPECT_FALSE(single.updateGiveItem(
        SHAMapNodeType::TnAccountState, makeShamapitem(probe, std::move(other))));
}

TEST_F(SHAMapTraversal, bounds_on_empty_map_return_end)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    // An empty map still leaves its root on the path, so end() here is an answer rather than a
    // refusal. This is what stops boundHelper from reading an empty path as an empty map.
    EXPECT_EQ(map.upperBound(uint256{}), map.end());
    EXPECT_EQ(map.lowerBound(uint256{}), map.end());

    uint256 probe;
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

    // fillMap uses addItem, which leaves root_ the inner node the map was constructed with and the
    // single leaf one level below it. So the path holds both, and boundHelper judges the leaf
    // first; for a probe the leaf does not qualify against it pops back to the root, whose scan
    // finds nothing on the requested side.
    uint256 below = key;
    --below;
    uint256 above = key;
    ++above;

    EXPECT_EQ(map.upperBound(below)->key(), key);
    EXPECT_EQ(map.upperBound(key), map.end());
    EXPECT_EQ(map.upperBound(above), map.end());

    EXPECT_EQ(map.lowerBound(above)->key(), key);
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

    std::vector<uint256> expected;
    for (std::size_t k = 1; k < keys.size(); k += 2)
        expected.push_back(keys[k]);

    std::vector<uint256> visited;
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
    auto const sentinel = uint256{std::string_view{std::string(64, '0')}};

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

    std::vector<uint256> const expected{sentinel, fanOutKeys.back()};
    std::vector<uint256> visited;
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
    std::vector<uint256> visited;
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
            uint256 const probe{std::string_view{text}};

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

    std::vector<uint256> expected;
    for (std::size_t k = 1; k < keys.size(); k += 2)
        expected.push_back(keys[k]);

    std::vector<uint256> visited;
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

    uint256 key;
    uint256 rootHash;
    std::vector<Blob> goodPath;

    static constexpr unsigned char kFirstKey = 1;
    static constexpr unsigned char kKeyCount = 100;
    static constexpr unsigned char kLastKey = kKeyCount - 1;

    for (unsigned char c = kFirstKey; c < kKeyCount; ++c)
    {
        uint256 k(c);
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
            uint256 const wrongKey(c + 1);
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
// the forged-path case below must not simply tighten the length limit.
TEST_F(SHAMapPathProof, legitimate_deep_path_is_sixty_five_elements)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    auto const kA = uint256{std::string_view{std::string(63, 'a') + "1"}};
    auto const kB = uint256{std::string_view{std::string(63, 'a') + "2"}};

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

// A forged path of 65 hash-chained inner nodes reaches depth kLeafDepth, where only the leaf
// terminating the path may sit. Such a path must be rejected.
TEST_F(SHAMapPathProof, all_inner_path_at_leaf_depth_is_rejected)
{
    // An arbitrary well-formed key; the test does not care about its specific value.
    constexpr uint256 kTestKey("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

    // Build upwards from the deepest node so each parent's selected branch carries its child's hash
    // and the hash chain validates at every level.
    std::vector<Blob> path;
    SHAMapHash childHash{uint256{1}};

    for (auto depth = SHAMap::kLeafDepth + 1u; depth-- > 0;)
    {
        auto const id = SHAMapNodeID::createID(std::min(depth, SHAMap::kLeafDepth - 1u), kTestKey);
        auto const branch = selectBranch(id, kTestKey);

        Serializer s;
        for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
            s.addBitString(i == branch ? childHash.asUInt256() : uint256{});
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
static std::pair<std::vector<Blob>, uint256>
forgeRootOverLeaf(Blob const& leafBlob, uint256 const& key)
{
    auto leaf = SHAMapTreeNode::makeFromWire(makeSlice(leafBlob));
    if (!leaf || !leaf->isLeaf())
        return {};
    leaf->updateHash();

    auto const branch = selectBranch(SHAMapNodeID::createID(0, key), key);
    Serializer s;
    for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
        s.addBitString(i == branch ? leaf->getHash().asUInt256() : uint256{});
    s.add8(kWireTypeInner);

    auto root = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    if (!root)
        return {};
    root->updateHash();

    return {std::vector<Blob>{leafBlob, s.getData()}, root->getHash().asUInt256()};
}

// The hash chain above a leaf proves nothing about which key that leaf holds, so a peer can graft a
// genuine leaf from elsewhere in the map onto a path forged for another key. Comparing the terminal
// leaf's own key against the key being proved is what rejects it.
TEST_F(SHAMapPathProof, substituted_leaf_for_other_key_is_rejected)
{
    tests::TestNodeFamily f{j_};
    SHAMap map{SHAMapType::FREE, f};
    map.setUnbacked();

    // Two arbitrary keys differing in their first nibble, so each leaf hangs off the root directly.
    constexpr uint256 kKey("1c8cec8e5e9b0e5e0e0f5b3e2c9f7a1d6b4e8c2a0d7f3b9e5c1a8d4f2b6e0c93");
    constexpr uint256 kOtherKey("e3f1a7d5b9c2e8f406a1d3b5c7e9f2a4d6b8c0e2f4a6d8b0c2e4f6a8d0b2c4e6");

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
 * A filter that resolves exactly one node, by hash.
 *
 * Stands in for the real sync filters, which serve a node from a local cache
 * keyed on its hash and so say nothing about where in a tree it belongs.
 */
class OneNodeFilter : public SHAMapSyncFilter
{
    std::map<SHAMapHash, Blob> nodes_;

public:
    OneNodeFilter(SHAMapHash const& hash, Blob blob)
    {
        nodes_.emplace(hash, std::move(blob));
    }

    explicit OneNodeFilter(std::vector<std::pair<SHAMapHash, Blob>> nodes)
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
// covers a node's contents rather than its position. Such a tree is what a proposer builds, and it
// is accepted node by node, so the paths that hook a node are where the position has to be judged.
class SHAMapMisplacedLeaf : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    // An arbitrary key whose first nibble is 1, so its leaf belongs under branch 1 of the root.
    static constexpr uint256 kKey{
        "1c8cec8e5e9b0e5e0e0f5b3e2c9f7a1d6b4e8c2a0d7f3b9e5c1a8d4f2b6e0c93"};

    // Any branch other than the one kKey selects at depth 0.
    static constexpr unsigned int kWrongBranch = 5;

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
     * Assemble `map` as a root inner node holding a leaf's hash under the
     * wrong branch.
     *
     * The root is installed directly, as a peer's would be, so the leaf itself
     * stays unresolved until a walk consults the filter for it.
     *
     * @param map the map to assemble, which must be synching and empty.
     * @param leafHash the hash the forged root records under kWrongBranch.
     * @return whether the root was accepted.
     */
    static bool
    forgeRoot(SHAMap& map, SHAMapHash const& leafHash)
    {
        Serializer s;
        for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
            s.addBitString(i == kWrongBranch ? leafHash.asUInt256() : uint256{});
        s.add8(kWireTypeInner);

        auto root = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
        if (!root)
            return false;
        root->updateHash();

        auto const rootHash = root->getHash();
        return map.addRootNode(rootHash, std::move(root), nullptr).isGood();
    }
};

// getMissingNodes reaches a filter through descendAsync, which hooks whatever it resolves. The
// verdict lands on the map, since every node from the root down hash-verified to get here.
TEST_F(SHAMapMisplacedLeaf, walking_for_missing_nodes_invalidates_the_map)
{
    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(leafBlob.empty());

    // Its own family, so the leaf is reachable only through the filter rather than from a cache the
    // source map warmed.
    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, uint256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, leafHash));
    ASSERT_TRUE(map.isValid());

    OneNodeFilter const filter{leafHash, leafBlob};
    map.getMissingNodes(1, &filter);

    EXPECT_FALSE(map.isValid());
}

// addKnownNode reaches a filter through the synchronous descend on its way to the position it was
// given, which is the other route a node takes into a tree during acquisition.
TEST_F(SHAMapMisplacedLeaf, hooking_a_known_node_invalidates_the_map)
{
    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(leafBlob.empty());

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, uint256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, leafHash));
    ASSERT_TRUE(map.isValid());

    // A key whose first nibble is kWrongBranch, so the walk descends the branch holding the leaf.
    // An inner node is offered rather than a leaf, since a leaf would have to agree with this
    // position and the point here is to reach the descent, not to hook what is offered.
    auto const target = SHAMapNodeID::createID(
        2, uint256{"5000000000000000000000000000000000000000000000000000000000000000"});

    Serializer s;
    for (auto i = 0u; i < SHAMap::kBranchFactor; ++i)
        s.addBitString(i == 0u ? uint256{1} : uint256{});
    s.add8(kWireTypeInner);
    auto offered = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    ASSERT_TRUE(offered);
    offered->updateHash();

    OneNodeFilter const filter{leafHash, leafBlob};
    auto const result = map.addKnownNode(target, std::move(offered), &filter);

    EXPECT_FALSE(map.isValid());

    // The verdict matters as much as the state: it is what the acquisition paths charge a peer on,
    // so a later change to it should fail here rather than pass quietly.
    EXPECT_TRUE(result.isInvalid());
    EXPECT_FALSE(result.isGood());
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
    SHAMap map{SHAMapType::FREE, uint256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, leafHash));
    ASSERT_TRUE(map.isValid());

    // The branch the forged root files the leaf under, which is not the one kKey selects.
    uint256 wrongPrefix;
    wrongPrefix.begin()[0] = static_cast<std::uint8_t>(kWrongBranch << 4);
    auto const target = SHAMapNodeID::createID(1, wrongPrefix);

    auto offered = SHAMapTreeNode::makeFromPrefix(makeSlice(leafBlob), leafHash);
    ASSERT_TRUE(offered);
    ASSERT_TRUE(offered->isLeaf());

    // No filter, so the walk resolves nothing locally and the node offered here is the one that
    // would be hooked.
    auto const result = map.addKnownNode(target, std::move(offered), nullptr);

    EXPECT_FALSE(map.isValid());
    EXPECT_TRUE(result.isInvalid());
    EXPECT_FALSE(result.isGood());
}

// A whole subtree can sit under the wrong branch through a single wrong child pointer, and that is
// cheaper to produce than one misplaced leaf. Every leaf below such a subtree agrees with its own
// final branch, because the subtree is internally well formed, and disagrees only at the level the
// pointer is wrong. So judging a leaf against the last branch alone accepts all of them, and only
// judging it against every branch above it refuses them.
TEST_F(SHAMapMisplacedLeaf, iterating_a_misplaced_subtree_throws)
{
    // Two keys sharing their first nibble, so they hang off one inner node at depth 1.
    constexpr uint256 kFirst{"a100000000000000000000000000000000000000000000000000000000000000"};
    constexpr uint256 kSecond{"a200000000000000000000000000000000000000000000000000000000000000"};

    tests::TestNodeFamily sourceFamily{j_};
    SHAMap source{SHAMapType::FREE, sourceFamily};
    source.setUnbacked();
    for (auto const& k : {kFirst, kSecond})
    {
        ASSERT_TRUE(source.addItem(
            SHAMapNodeType::TnAccountState, makeShamapitem(k, Slice{k.data(), k.size()})));
    }
    source.invariants();

    // The inner node holding both leaves, as the filter will serve it. It belongs under branch 10,
    // the nibble the two keys share, and the forged root below files it under kWrongBranch instead.
    auto const subtree = source.getProofPath(kFirst);
    ASSERT_TRUE(subtree.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) has_value() checked above
    ASSERT_GE(subtree->size(), 2u);

    // getProofPath returns the path deepest element first, so the element above the leaf is the
    // inner node the two keys share.
    auto inner = SHAMapTreeNode::makeFromWire(makeSlice((*subtree)[1]));
    // NOLINTEND(bugprone-unchecked-optional-access)
    ASSERT_TRUE(inner);
    ASSERT_TRUE(inner->isInner());
    inner->updateHash();

    Serializer innerPrefixed;
    inner->serializeWithPrefix(innerPrefixed);

    // Both leaves are served as well. Without them the walk would stop on a node it genuinely does
    // not have, and the throw below would say nothing about position.
    std::vector<std::pair<SHAMapHash, Blob>> served;
    served.emplace_back(inner->getHash(), innerPrefixed.getData());
    for (auto const& k : {kFirst, kSecond})
    {
        auto const leafPath = source.getProofPath(k);
        ASSERT_TRUE(leafPath.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) has_value() checked above
        ASSERT_FALSE(leafPath->empty());
        auto leaf = SHAMapTreeNode::makeFromWire(makeSlice(leafPath->front()));
        // NOLINTEND(bugprone-unchecked-optional-access)
        ASSERT_TRUE(leaf);
        ASSERT_TRUE(leaf->isLeaf());
        leaf->updateHash();

        Serializer leafPrefixed;
        leaf->serializeWithPrefix(leafPrefixed);
        served.emplace_back(leaf->getHash(), leafPrefixed.getData());
    }

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, uint256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, inner->getHash()));

    // The inner node itself carries no key, so nothing about it is out of place. Only a leaf below
    // it can show that the branch it was reached through disagrees with the keys underneath.
    OneNodeFilter const filter{std::move(served)};
    map.getMissingNodes(4, &filter);

    EXPECT_THROW(map.begin(), SHAMapMissingNode);

    // The bounds have to refuse the same map, and refusing is not the same as answering end().
    // This probe selects kWrongBranch at depth 0 and then the branch holding kFirst, so the walk
    // reaches the misplaced leaf and clears the path. Both keys in the map are greater than the
    // probe, so end() here would be the positive and wrong claim that no greater key exists.
    uint256 probe;
    probe.begin()[0] = static_cast<std::uint8_t>((kWrongBranch << 4) | 0x1u);
    ASSERT_GT(kFirst, probe);
    ASSERT_GT(kSecond, probe);

    EXPECT_THROW(map.upperBound(probe), SHAMapMissingNode);
    EXPECT_THROW(map.lowerBound(probe), SHAMapMissingNode);
}

// The descendAsync walk leaves the leaf hooked, since it resolved the node before the position
// could be judged. Iterating it must not abort an instrumented build, and must not report the map
// as empty either, which is what a plain nullptr from belowHelper would have meant.
TEST_F(SHAMapMisplacedLeaf, iterating_a_hooked_misplaced_leaf_throws)
{
    tests::TestNodeFamily sourceFamily{j_};
    auto const [leafBlob, leafHash] = genuineLeaf(sourceFamily);
    ASSERT_FALSE(leafBlob.empty());

    tests::TestNodeFamily targetFamily{j_};
    SHAMap map{SHAMapType::FREE, uint256{}, targetFamily};
    map.setUnbacked();
    ASSERT_TRUE(forgeRoot(map, leafHash));

    OneNodeFilter const filter{leafHash, leafBlob};
    map.getMissingNodes(1, &filter);
    ASSERT_FALSE(map.isValid());

    EXPECT_THROW(map.begin(), SHAMapMissingNode);
}

}  // namespace xrpl::tests
