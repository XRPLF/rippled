//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Blob.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/shamap/SHAMap.h>
#include <test/shamap/common.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {
namespace tests {

#ifndef __INTELLISENSE__
static_assert(std::is_nothrow_destructible<SHAMap>{}, "");
static_assert(!std::is_default_constructible<SHAMap>{}, "");
static_assert(!std::is_copy_constructible<SHAMap>{}, "");
static_assert(!std::is_copy_assignable<SHAMap>{}, "");
static_assert(!std::is_move_constructible<SHAMap>{}, "");
static_assert(!std::is_move_assignable<SHAMap>{}, "");

static_assert(std::is_nothrow_destructible<SHAMap::const_iterator>{}, "");
static_assert(std::is_copy_constructible<SHAMap::const_iterator>{}, "");
static_assert(std::is_copy_assignable<SHAMap::const_iterator>{}, "");
static_assert(std::is_move_constructible<SHAMap::const_iterator>{}, "");
static_assert(std::is_move_assignable<SHAMap::const_iterator>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapItem>{}, "");
static_assert(!std::is_default_constructible<SHAMapItem>{}, "");
static_assert(std::is_copy_constructible<SHAMapItem>{}, "");
static_assert(std::is_copy_assignable<SHAMapItem>{}, "");
static_assert(std::is_move_constructible<SHAMapItem>{}, "");
static_assert(std::is_move_assignable<SHAMapItem>{}, "");

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
operator==(SHAMapItem const& a, SHAMapItem const& b)
{
    return a.key() == b.key();
}
inline bool
operator!=(SHAMapItem const& a, SHAMapItem const& b)
{
    return a.key() != b.key();
}
inline bool
operator==(SHAMapItem const& a, uint256 const& b)
{
    return a.key() == b;
}
inline bool
operator!=(SHAMapItem const& a, uint256 const& b)
{
    return a.key() != b;
}

class SHAMap_test : public beast::unit_test::suite
{
public:
    static Blob
    IntToVUC(int v)
    {
        Blob vuc;

        for (int i = 0; i < 32; ++i)
            vuc.push_back(static_cast<unsigned char>(v));

        return vuc;
    }

    void
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("SHAMap_test", *this);

        run(true, journal);
        run(false, journal);
    }

    void
    run(bool backed, beast::Journal const& journal)
    {
        if (backed)
            testcase("add/traverse backed");
        else
            testcase("add/traverse unbacked");

        tests::TestNodeFamily f(journal);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        (void)h1.parseHex(
            "092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        (void)h2.parseHex(
            "436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        (void)h3.parseHex(
            "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        (void)h4.parseHex(
            "b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        (void)h5.parseHex(
            "a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap(SHAMapType::FREE, f);
        sMap.invariants();
        if (!backed)
            sMap.setUnbacked();

        SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)),
            i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));
        unexpected(
            !sMap.addItem(SHAMapNodeType::tnTRANSACTION_NM, SHAMapItem{i2}),
            "no add");
        sMap.invariants();
        unexpected(
            !sMap.addItem(SHAMapNodeType::tnTRANSACTION_NM, SHAMapItem{i1}),
            "no add");
        sMap.invariants();

        auto i = sMap.begin();
        auto e = sMap.end();
        unexpected(i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected(i == e || (*i != i2), "bad traverse");
        ++i;
        unexpected(i != e, "bad traverse");
        sMap.addItem(SHAMapNodeType::tnTRANSACTION_NM, SHAMapItem{i4});
        sMap.invariants();
        sMap.delItem(i2.key());
        sMap.invariants();
        sMap.addItem(SHAMapNodeType::tnTRANSACTION_NM, SHAMapItem{i3});
        sMap.invariants();
        i = sMap.begin();
        e = sMap.end();
        unexpected(i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected(i == e || (*i != i3), "bad traverse");
        ++i;
        unexpected(i == e || (*i != i4), "bad traverse");
        ++i;
        unexpected(i != e, "bad traverse");

        if (backed)
            testcase("snapshot backed");
        else
            testcase("snapshot unbacked");

        SHAMapHash mapHash = sMap.getHash();
        std::shared_ptr<SHAMap> map2 = sMap.snapShot(false);
        map2->invariants();
        unexpected(sMap.getHash() != mapHash, "bad snapshot");
        unexpected(map2->getHash() != mapHash, "bad snapshot");

        SHAMap::Delta delta;
        BEAST_EXPECT(sMap.compare(*map2, delta, 100));
        BEAST_EXPECT(delta.empty());

        unexpected(!sMap.delItem(sMap.begin()->key()), "bad mod");
        sMap.invariants();
        unexpected(sMap.getHash() == mapHash, "bad snapshot");
        unexpected(map2->getHash() != mapHash, "bad snapshot");

        BEAST_EXPECT(sMap.compare(*map2, delta, 100));
        BEAST_EXPECT(delta.size() == 1);
        BEAST_EXPECT(delta.begin()->first == h1);
        BEAST_EXPECT(delta.begin()->second.first == nullptr);
        BEAST_EXPECT(delta.begin()->second.second->key() == h1);

        sMap.dump();

        if (backed)
            testcase("build/tear backed");
        else
            testcase("build/tear unbacked");
        {
            std::vector<uint256> keys(8);
            (void)keys[0].parseHex(
                "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[1].parseHex(
                "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[2].parseHex(
                "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[3].parseHex(
                "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[4].parseHex(
                "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[5].parseHex(
                "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[6].parseHex(
                "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[7].parseHex(
                "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");

            std::vector<uint256> hashes(8);
            (void)hashes[0].parseHex(
                "B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F"
                "7F");
            (void)hashes[1].parseHex(
                "FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE2"
                "66");
            (void)hashes[2].parseHex(
                "4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B"
                "75");
            (void)hashes[3].parseHex(
                "7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E"
                "07");
            (void)hashes[4].parseHex(
                "395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A"
                "3E");
            (void)hashes[5].parseHex(
                "D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DD"
                "E4");
            (void)hashes[6].parseHex(
                "76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF943611436"
                "15");
            (void)hashes[7].parseHex(
                "DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230"
                "E5");

            SHAMap map(SHAMapType::FREE, f);
            if (!backed)
                map.setUnbacked();

            BEAST_EXPECT(map.getHash() == beast::zero);
            for (int k = 0; k < keys.size(); ++k)
            {
                SHAMapItem item(keys[k], IntToVUC(k));
                BEAST_EXPECT(map.addItem(
                    SHAMapNodeType::tnTRANSACTION_NM, std::move(item)));
                BEAST_EXPECT(map.getHash().as_uint256() == hashes[k]);
                map.invariants();
            }
            for (int k = keys.size() - 1; k >= 0; --k)
            {
                BEAST_EXPECT(map.getHash().as_uint256() == hashes[k]);
                BEAST_EXPECT(map.delItem(keys[k]));
                map.invariants();
            }
            BEAST_EXPECT(map.getHash() == beast::zero);
        }

        if (backed)
            testcase("iterate backed");
        else
            testcase("iterate unbacked");

        {
            std::vector<uint256> keys(8);
            (void)keys[0].parseHex(
                "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[1].parseHex(
                "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[2].parseHex(
                "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[3].parseHex(
                "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[4].parseHex(
                "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[5].parseHex(
                "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[6].parseHex(
                "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            (void)keys[7].parseHex(
                "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");

            tests::TestNodeFamily tf{journal};
            SHAMap map{SHAMapType::FREE, tf};
            if (!backed)
                map.setUnbacked();
            for (auto const& k : keys)
            {
                map.addItem(
                    SHAMapNodeType::tnTRANSACTION_NM,
                    SHAMapItem{k, IntToVUC(0)});
                map.invariants();
            }

            int h = 7;
            for (auto const& k : map)
            {
                BEAST_EXPECT(k.key() == keys[h]);
                --h;
            }
        }
    }
};

class SHAMapPathProof_test : public beast::unit_test::suite
{
    void
    run() override
    {
        test::SuiteJournal journal("SHAMapPathProof_test", *this);

        tests::TestNodeFamily tf{journal};
        SHAMap map{SHAMapType::FREE, tf};
        map.setUnbacked();

        uint256 key;
        uint256 rootHash;
        std::vector<Blob> goodPath;

        for (unsigned char c = 1; c < 100; ++c)
        {
            uint256 k(c);
            Blob b(32, c);
            map.addItem(SHAMapNodeType::tnACCOUNT_STATE, SHAMapItem{k, b});
            map.invariants();

            auto root = map.getHash().as_uint256();
            auto path = map.getProofPath(k);
            BEAST_EXPECT(path);
            if (!path)
                break;
            BEAST_EXPECT(map.verifyProofPath(root, k, *path));
            if (c == 1)
            {
                // extra node
                path->insert(path->begin(), path->front());
                BEAST_EXPECT(!map.verifyProofPath(root, k, *path));
                // wrong key
                uint256 wrongKey(c + 1);
                BEAST_EXPECT(!map.getProofPath(wrongKey));
            }
            if (c == 99)
            {
                key = k;
                rootHash = root;
                goodPath = std::move(*path);
            }
        }

        // still good
        BEAST_EXPECT(map.verifyProofPath(rootHash, key, goodPath));
        // empty path
        std::vector<Blob> badPath;
        BEAST_EXPECT(!map.verifyProofPath(rootHash, key, badPath));
        // too long
        badPath = goodPath;
        badPath.push_back(goodPath.back());
        BEAST_EXPECT(!map.verifyProofPath(rootHash, key, badPath));
        // bad node
        badPath.clear();
        badPath.emplace_back(100, 100);
        BEAST_EXPECT(!map.verifyProofPath(rootHash, key, badPath));
        // bad node type
        badPath.clear();
        badPath.push_back(goodPath.front());
        badPath.front().back()--;  // change node type
        BEAST_EXPECT(!map.verifyProofPath(rootHash, key, badPath));
        // all inner
        badPath.clear();
        badPath = goodPath;
        badPath.erase(badPath.begin());
        BEAST_EXPECT(!map.verifyProofPath(rootHash, key, badPath));
    }
};

BEAST_DEFINE_TESTSUITE(SHAMap, ripple_app, ripple);
BEAST_DEFINE_TESTSUITE(SHAMapPathProof, ripple_app, ripple);
}  // namespace tests
}  // namespace ripple
