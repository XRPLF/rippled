#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::tests {

#ifndef __INTELLISENSE__
static_assert(std::is_nothrow_destructible<SHAMap>{}, "");
static_assert(!std::is_default_constructible<SHAMap>{}, "");
static_assert(!std::is_copy_constructible<SHAMap>{}, "");
static_assert(!std::is_copy_assignable<SHAMap>{}, "");
static_assert(!std::is_move_constructible<SHAMap>{}, "");
static_assert(!std::is_move_assignable<SHAMap>{}, "");

static_assert(std::is_nothrow_destructible<SHAMap::ConstIterator>{}, "");
static_assert(std::is_copy_constructible<SHAMap::ConstIterator>{}, "");
static_assert(std::is_copy_assignable<SHAMap::ConstIterator>{}, "");
static_assert(std::is_move_constructible<SHAMap::ConstIterator>{}, "");
static_assert(std::is_move_assignable<SHAMap::ConstIterator>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapItem>{}, "");
static_assert(!std::is_default_constructible<SHAMapItem>{}, "");
static_assert(!std::is_copy_constructible<SHAMapItem>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapNodeID>{}, "");
static_assert(std::is_default_constructible<SHAMapNodeID>{}, "");
static_assert(std::is_copy_constructible<SHAMapNodeID>{}, "");
static_assert(std::is_copy_assignable<SHAMapNodeID>{}, "");
static_assert(std::is_move_constructible<SHAMapNodeID>{}, "");
static_assert(std::is_move_assignable<SHAMapNodeID>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapHash>{}, "");
static_assert(std::is_default_constructible<SHAMapHash>{}, "");
static_assert(std::is_copy_constructible<SHAMapHash>{}, "");
static_assert(std::is_copy_assignable<SHAMapHash>{}, "");
static_assert(std::is_move_constructible<SHAMapHash>{}, "");
static_assert(std::is_move_assignable<SHAMapHash>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapTreeNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapTreeNode>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapInnerNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapInnerNode>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapLeafNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapLeafNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapLeafNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapLeafNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapLeafNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapLeafNode>{}, "");
#endif

inline bool
operator!=(SHAMapItem const& a, SHAMapItem const& b)
{
    return a.key() != b.key();
}

class SHAMapTest : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    static Buffer
    intToVuc(int v)
    {
        Buffer vuc(32);
        std::fill_n(vuc.data(), vuc.size(), static_cast<std::uint8_t>(v));
        return vuc;
    }

    void
    run(bool backed)
    {
        tests::TestNodeFamily f(j_);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        constexpr uint256 kH1("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        constexpr uint256 kH2("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        constexpr uint256 kH3("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        constexpr uint256 kH4("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");

        SHAMap sMap(SHAMapType::FREE, f);
        sMap.invariants();
        if (!backed)
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
            constexpr std::array kEYS{
                uint256(
                    "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8")};

            constexpr std::array kHASHES{
                uint256(
                    "B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C"
                    "64B6281F7F"),
                uint256(
                    "FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A883184546"
                    "7FB2ECE266"),
                uint256(
                    "4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51A"
                    "E756795B75"),
                uint256(
                    "7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC"
                    "6C74F93E07"),
                uint256(
                    "395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596"
                    "462B0E3A3E"),
                uint256(
                    "D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D9"
                    "8A84D9DDE4"),
                uint256(
                    "76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF9"
                    "4361143615"),
                uint256(
                    "DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA0"
                    "2BBE7230E5")};

            SHAMap map(SHAMapType::FREE, f);
            if (!backed)
                map.setUnbacked();

            EXPECT_EQ(map.getHash(), beast::kZero);
            for (std::size_t k = 0; k < kEYS.size(); ++k)
            {
                EXPECT_TRUE(map.addItem(
                    SHAMapNodeType::TnTransactionNm,
                    makeShamapitem(kEYS[k], intToVuc(static_cast<int>(k)))));
                EXPECT_EQ(map.getHash().asUInt256(), kHASHES[k]);
                map.invariants();
            }
            for (std::size_t k = kEYS.size(); k-- > 0;)
            {
                EXPECT_EQ(map.getHash().asUInt256(), kHASHES[k]);
                EXPECT_TRUE(map.delItem(kEYS[k]));
                map.invariants();
            }
            EXPECT_EQ(map.getHash(), beast::kZero);
        }

        {
            constexpr std::array kEYS{
                uint256(
                    "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8"),
                uint256(
                    "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e"
                    "5a772c6ca8")};

            tests::TestNodeFamily tf{j_};
            SHAMap map{SHAMapType::FREE, tf};
            if (!backed)
                map.setUnbacked();
            for (auto const& k : kEYS)
            {
                map.addItem(SHAMapNodeType::TnTransactionNm, makeShamapitem(k, intToVuc(0)));
                map.invariants();
            }

            int h = 7;
            for (auto const& k : map)
            {
                EXPECT_EQ(k.key(), kEYS[h]);
                --h;
            }
        }
    }
};

TEST_F(SHAMapTest, add_traverse_snapshot_build_tear_and_iterate_backed)
{
    run(true);
}

TEST_F(SHAMapTest, add_traverse_snapshot_build_tear_and_iterate_unbacked)
{
    run(false);
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

    for (unsigned char c = 1; c < 100; ++c)
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
        if (c == 1)
        {
            // extra node
            proofPath.insert(proofPath.begin(), proofPath.front());
            EXPECT_FALSE(map.verifyProofPath(root, k, proofPath));
            // wrong key
            uint256 const wrongKey(c + 1);
            EXPECT_FALSE(map.getProofPath(wrongKey));
        }
        if (c == 99)
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

}  // namespace xrpl::tests
