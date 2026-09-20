#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

}  // namespace xrpl::tests
