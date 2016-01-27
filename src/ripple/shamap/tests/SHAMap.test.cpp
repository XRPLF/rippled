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

#include <BeastConfig.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/shamap/tests/common.h>
#include <ripple/basics/Blob.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {
namespace tests {

static_assert( std::is_nothrow_destructible <SHAMap>{}, "");
static_assert(!std::is_default_constructible<SHAMap>{}, "");
static_assert(!std::is_copy_constructible   <SHAMap>{}, "");
static_assert(!std::is_copy_assignable      <SHAMap>{}, "");
static_assert(!std::is_move_constructible   <SHAMap>{}, "");
static_assert(!std::is_move_assignable      <SHAMap>{}, "");

static_assert( std::is_nothrow_destructible        <SHAMap::version>{}, "");
static_assert(!std::is_default_constructible       <SHAMap::version>{}, "");
static_assert( std::is_trivially_copy_constructible<SHAMap::version>{}, "");
static_assert( std::is_trivially_copy_assignable   <SHAMap::version>{}, "");
static_assert( std::is_trivially_move_constructible<SHAMap::version>{}, "");
static_assert( std::is_trivially_move_assignable   <SHAMap::version>{}, "");

static_assert( std::is_nothrow_destructible <SHAMap::const_iterator>{}, "");
static_assert( std::is_default_constructible<SHAMap::const_iterator>{}, "");
static_assert( std::is_copy_constructible   <SHAMap::const_iterator>{}, "");
static_assert( std::is_copy_assignable      <SHAMap::const_iterator>{}, "");
static_assert( std::is_move_constructible   <SHAMap::const_iterator>{}, "");
static_assert( std::is_move_assignable      <SHAMap::const_iterator>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapItem>{}, "");
static_assert(!std::is_default_constructible<SHAMapItem>{}, "");
static_assert( std::is_copy_constructible   <SHAMapItem>{}, "");
static_assert( std::is_copy_assignable      <SHAMapItem>{}, "");
static_assert( std::is_move_constructible   <SHAMapItem>{}, "");
static_assert( std::is_move_assignable      <SHAMapItem>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapNodeID>{}, "");
static_assert( std::is_default_constructible<SHAMapNodeID>{}, "");
static_assert( std::is_copy_constructible   <SHAMapNodeID>{}, "");
static_assert( std::is_copy_assignable      <SHAMapNodeID>{}, "");
static_assert( std::is_move_constructible   <SHAMapNodeID>{}, "");
static_assert( std::is_move_assignable      <SHAMapNodeID>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapHash>{}, "");
static_assert( std::is_default_constructible<SHAMapHash>{}, "");
static_assert( std::is_copy_constructible   <SHAMapHash>{}, "");
static_assert( std::is_copy_assignable      <SHAMapHash>{}, "");
static_assert( std::is_move_constructible   <SHAMapHash>{}, "");
static_assert( std::is_move_assignable      <SHAMapHash>{}, "");

static_assert(!std::is_nothrow_destructible <SHAMapAbstractNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapAbstractNode>{}, "");
static_assert(!std::is_copy_constructible   <SHAMapAbstractNode>{}, "");
static_assert(!std::is_copy_assignable      <SHAMapAbstractNode>{}, "");
static_assert(!std::is_move_constructible   <SHAMapAbstractNode>{}, "");
static_assert(!std::is_move_assignable      <SHAMapAbstractNode>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapInnerNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_constructible   <SHAMapInnerNode>{}, "");
static_assert(!std::is_copy_assignable      <SHAMapInnerNode>{}, "");
static_assert(!std::is_move_constructible   <SHAMapInnerNode>{}, "");
static_assert(!std::is_move_assignable      <SHAMapInnerNode>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapInnerNodeV2>{}, "");
static_assert(!std::is_default_constructible<SHAMapInnerNodeV2>{}, "");
static_assert(!std::is_copy_constructible   <SHAMapInnerNodeV2>{}, "");
static_assert(!std::is_copy_assignable      <SHAMapInnerNodeV2>{}, "");
static_assert(!std::is_move_constructible   <SHAMapInnerNodeV2>{}, "");
static_assert(!std::is_move_assignable      <SHAMapInnerNodeV2>{}, "");

static_assert( std::is_nothrow_destructible <SHAMapTreeNode>{}, "");
static_assert(!std::is_default_constructible<SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_constructible   <SHAMapTreeNode>{}, "");
static_assert(!std::is_copy_assignable      <SHAMapTreeNode>{}, "");
static_assert(!std::is_move_constructible   <SHAMapTreeNode>{}, "");
static_assert(!std::is_move_assignable      <SHAMapTreeNode>{}, "");

inline bool operator== (SHAMapItem const& a, SHAMapItem const& b) { return a.key() == b.key(); }
inline bool operator!= (SHAMapItem const& a, SHAMapItem const& b) { return a.key() != b.key(); }
inline bool operator== (SHAMapItem const& a, uint256 const& b) { return a.key() == b; }
inline bool operator!= (SHAMapItem const& a, uint256 const& b) { return a.key() != b; }

class SHAMap_test : public beast::unit_test::suite
{
public:
    static Blob IntToVUC (int v)
    {
        Blob vuc;

        for (int i = 0; i < 32; ++i)
            vuc.push_back (static_cast<unsigned char> (v));

        return vuc;
    }

    void run ()
    {
        run (true,  SHAMap::version{1});
        run (false, SHAMap::version{1});
        run (true,  SHAMap::version{2});
        run (false, SHAMap::version{2});
    }

    void run (bool backed, SHAMap::version v)
    {
        if (backed)
            testcase ("add/traverse backed");
        else
            testcase ("add/traverse unbacked");

        beast::Journal const j;                            // debug journal
        tests::TestFamily f(j);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        h1.SetHex ("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        h2.SetHex ("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        h3.SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        h4.SetHex ("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        h5.SetHex ("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap (SHAMapType::FREE, f, v);
        sMap.invariants();
        if (! backed)
            sMap.setUnbacked ();

        SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));
        unexpected (!sMap.addItem (SHAMapItem{i2}, true, false), "no add");
        sMap.invariants();
        unexpected (!sMap.addItem (SHAMapItem{i1}, true, false), "no add");
        sMap.invariants();

        auto i = sMap.begin();
        auto e = sMap.end();
        unexpected (i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected (i == e || (*i != i2), "bad traverse");
        ++i;
        unexpected (i != e, "bad traverse");
        sMap.addItem (SHAMapItem{i4}, true, false);
        sMap.invariants();
        sMap.delItem (i2.key());
        sMap.invariants();
        sMap.addItem (SHAMapItem{i3}, true, false);
        sMap.invariants();
        i = sMap.begin();
        e = sMap.end();
        unexpected (i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected (i == e || (*i != i3), "bad traverse");
        ++i;
        unexpected (i == e || (*i != i4), "bad traverse");
        ++i;
        unexpected (i != e, "bad traverse");

        if (backed)
            testcase ("snapshot backed");
        else
            testcase ("snapshot unbacked");

        SHAMapHash mapHash = sMap.getHash ();
        std::shared_ptr<SHAMap> map2 = sMap.snapShot (false);
        map2->invariants();
        unexpected (sMap.getHash () != mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");

        SHAMap::Delta delta;
        expect(sMap.compare(*map2, delta, 100), "There should be no differences");
        expect(delta.empty(), "The delta should be empty");

        unexpected (!sMap.delItem (sMap.begin()->key()), "bad mod");
        sMap.invariants();
        unexpected (sMap.getHash () == mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");

        expect(sMap.compare(*map2, delta, 100), "There should be 1 difference");
        expect(delta.size() == 1, "The delta should be size 1");
        expect(delta.begin()->first == h1, "Should be the first key");
        expect(delta.begin()->second.first == nullptr, "Must be null");
        expect(delta.begin()->second.second->key() == h1, "The difference is the first key");

        sMap.dump();

        auto const is_v2 = sMap.is_v2();

        if (backed)
            testcase ("build/tear backed");
        else
            testcase ("build/tear unbacked");
        {
            std::vector<uint256> keys(8);
            keys[0].SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[1].SetHex ("b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[2].SetHex ("b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[3].SetHex ("b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[4].SetHex ("b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[5].SetHex ("b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[6].SetHex ("f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[7].SetHex ("292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

            std::vector<uint256> hashes(8);
            if (is_v2)
            {
                hashes[0].SetHex ("B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F7F");
                hashes[1].SetHex ("6A70885D21024F9F4F50E688B365D0E017266F53AE3E77B52AAEF84E167FE942");
                hashes[2].SetHex ("BA635322BDF510CCFC0578C194C1F87DA2D97C1A55A469F6E7A463BE963663D7");
                hashes[3].SetHex ("5F751361AC7A161DED4D6EAAC4B587C7C01E50E1F9EC3DD61207BD6B196E7DB1");
                hashes[4].SetHex ("FC1C57DD4BF15E37961E0B3064C43E60A9DC26EA332D7A6178FE2284901DB49F");
                hashes[5].SetHex ("4FCDFE944E8E19E35FF60E7BDA7DB21B1CD99670BCF158FEF8F0F8B49BF5C9AD");
                hashes[6].SetHex ("31F6DE8152FBE77EAD59805631FCDDB71F1BC6A5E9BD8FA3948D82D1CE1F93D6");
                hashes[7].SetHex ("00895AD3B161D483C4EF7B5469B0D305685222B5102C2C3F614FD84AD50C4B14");
            }
            else
            {
                hashes[0].SetHex ("B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F7F");
                hashes[1].SetHex ("FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE266");
                hashes[2].SetHex ("4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B75");
                hashes[3].SetHex ("7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E07");
                hashes[4].SetHex ("395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A3E");
                hashes[5].SetHex ("D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DDE4");
                hashes[6].SetHex ("76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF94361143615");
                hashes[7].SetHex ("DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230E5");
            }

           SHAMap map (SHAMapType::FREE, f, v);
            if (! backed)
                map.setUnbacked ();

            expect (map.getHash() == zero, "bad initial empty map hash");
            for (int i = 0; i < keys.size(); ++i)
            {
                SHAMapItem item (keys[i], IntToVUC (i));
                expect (map.addItem (std::move(item), true, false), "unable to add item");
                expect (map.getHash().as_uint256() == hashes[i], "bad buildup map hash");
                map.invariants();
            }
            if (v == SHAMap::version{1})
            {
                expect(!map.is_v2(), "map should be version 1");
                auto map_v2 = map.make_v2();
                expect(map_v2 != nullptr, "make_v2 should never return nullptr");
                expect(map_v2->is_v2(), "map should be version 2");
                map_v2->invariants();
                auto i1 = map.begin();
                auto e1 = map.end();
                auto i2 = map_v2->begin();
                auto e2 = map_v2->end();
                for (; i1 != e1; ++i1, ++i2)
                {
                    expect(i2 != e2, "make_v2 size mismatch");
                    expect(*i1 == *i2, "make_v2, item mismatch");
                }
                expect(i2 == e2, "make_v2 size mismatch");
            }
            for (int i = keys.size() - 1; i >= 0; --i)
            {
                expect (map.getHash().as_uint256() == hashes[i], "bad teardown hash");
                expect (map.delItem (keys[i]), "unable to remove item");
                map.invariants();
            }
            expect (map.getHash() == zero, "bad final empty map hash");
        }

        if (backed)
            testcase ("iterate backed");
        else
            testcase ("iterate unbacked");

        {
            std::vector<uint256> keys(8);
            keys[0].SetHex ("f22891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[1].SetHex ("b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[2].SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[3].SetHex ("b92881fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[4].SetHex ("b92791fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[5].SetHex ("b92691fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[6].SetHex ("b91891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
            keys[7].SetHex ("292891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

            tests::TestFamily f{beast::Journal{}};
            SHAMap map{SHAMapType::FREE, f, v};
            if (! backed)
                map.setUnbacked ();
            for (auto const& k : keys)
            {
                map.addItem(SHAMapItem{k, IntToVUC(0)}, true, false);
                map.invariants();
            }

            int i = 7;
            for (auto const& k : map)
            {
                expect(k.key() == keys[i]);
                --i;
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(SHAMap,ripple_app,ripple);

} // tests
} // ripple
