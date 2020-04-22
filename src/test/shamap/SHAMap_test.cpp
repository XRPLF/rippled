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
static_assert(std::is_default_constructible<SHAMap::const_iterator>{}, "");
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

static_assert(!std::is_nothrow_destructible<SHAMapAbstractNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapAbstractNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapAbstractNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapAbstractNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapAbstractNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapAbstractNode>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapInnerNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapInnerNode>{}, "");

static_assert(std::is_nothrow_destructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_assignable<SHAMapTreeNode>{}, "");
static_assert(!std::is_move_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_move_assignable<SHAMapTreeNode>{}, "");
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

        tests::TestFamily f(journal);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        h1.SetHex(
            "092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        h2.SetHex(
            "436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        h3.SetHex(
            "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        h4.SetHex(
            "b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        h5.SetHex(
            "a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap(SHAMapType::FREE, f);
        sMap.invariants();
        if (!backed)
            sMap.setUnbacked();

        SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)),
            i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));
        unexpected(!sMap.addItem(SHAMapItem{i2}, true, false), "no add");
        sMap.invariants();
        unexpected(!sMap.addItem(SHAMapItem{i1}, true, false), "no add");
        sMap.invariants();

        auto i = sMap.begin();
        auto e = sMap.end();
        unexpected(i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected(i == e || (*i != i2), "bad traverse");
        ++i;
        unexpected(i != e, "bad traverse");
        sMap.addItem(SHAMapItem{i4}, true, false);
        sMap.invariants();
        sMap.delItem(i2.key());
        sMap.invariants();
        sMap.addItem(SHAMapItem{i3}, true, false);
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
            keys[0].SetHex(
                "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[1].SetHex(
                "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[2].SetHex(
                "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[3].SetHex(
                "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[4].SetHex(
                "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[5].SetHex(
                "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[6].SetHex(
                "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[7].SetHex(
                "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");

            std::vector<uint256> hashes(8);
            hashes[0].SetHex(
                "B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F"
                "7F");
            hashes[1].SetHex(
                "FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE2"
                "66");
            hashes[2].SetHex(
                "4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B"
                "75");
            hashes[3].SetHex(
                "7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E"
                "07");
            hashes[4].SetHex(
                "395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A"
                "3E");
            hashes[5].SetHex(
                "D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DD"
                "E4");
            hashes[6].SetHex(
                "76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF943611436"
                "15");
            hashes[7].SetHex(
                "DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230"
                "E5");

            SHAMap map(SHAMapType::FREE, f);
            if (!backed)
                map.setUnbacked();

            BEAST_EXPECT(map.getHash() == beast::zero);
            for (int k = 0; k < keys.size(); ++k)
            {
                SHAMapItem item(keys[k], IntToVUC(k));
                BEAST_EXPECT(map.addItem(std::move(item), true, false));
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
            keys[0].SetHex(
                "f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[1].SetHex(
                "b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[2].SetHex(
                "b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[3].SetHex(
                "b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[4].SetHex(
                "b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[5].SetHex(
                "b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[6].SetHex(
                "b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");
            keys[7].SetHex(
                "292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6c"
                "a8");

            tests::TestFamily tf{journal};
            SHAMap map{SHAMapType::FREE, tf};
            if (!backed)
                map.setUnbacked();
            for (auto const& k : keys)
            {
                map.addItem(SHAMapItem{k, IntToVUC(0)}, true, false);
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

BEAST_DEFINE_TESTSUITE(SHAMap, ripple_app, ripple);

}  // namespace tests
}  // namespace ripple
