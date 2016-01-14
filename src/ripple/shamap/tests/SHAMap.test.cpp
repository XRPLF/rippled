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
#include <beast/unit_test/suite.h>
#include <beast/utility/Journal.h>

namespace ripple {
namespace tests {

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
        run (true);
        run (false);
    }

    void run (bool backed)
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

        SHAMap sMap (SHAMapType::FREE, f);
        if (! backed)
            sMap.setUnbacked ();

        SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));
        unexpected (!sMap.addItem (SHAMapItem{i2}, true, false), "no add");
        unexpected (!sMap.addItem (SHAMapItem{i1}, true, false), "no add");

        auto i = sMap.begin();
        auto e = sMap.end();
        unexpected (i == e || (*i != i1), "bad traverse");
        ++i;
        unexpected (i == e || (*i != i2), "bad traverse");
        ++i;
        unexpected (i != e, "bad traverse");
        sMap.addItem (SHAMapItem{i4}, true, false);
        sMap.delItem (i2.key());
        sMap.addItem (SHAMapItem{i3}, true, false);
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
        unexpected (sMap.getHash () != mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
        unexpected (!sMap.delItem (sMap.begin()->key()), "bad mod");
        unexpected (sMap.getHash () == mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
        sMap.dump();

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
            hashes[0].SetHex ("B7387CFEA0465759ADC718E8C42B52D2309D179B326E239EB5075C64B6281F7F");
            hashes[1].SetHex ("FBC195A9592A54AB44010274163CB6BA95F497EC5BA0A8831845467FB2ECE266");
            hashes[2].SetHex ("4E7D2684B65DFD48937FFB775E20175C43AF0C94066F7D5679F51AE756795B75");
            hashes[3].SetHex ("7A2F312EB203695FFD164E038E281839EEF06A1B99BFC263F3CECC6C74F93E07");
            hashes[4].SetHex ("395A6691A372387A703FB0F2C6D2C405DAF307D0817F8F0E207596462B0E3A3E");
            hashes[5].SetHex ("D044C0A696DE3169CC70AE216A1564D69DE96582865796142CE7D98A84D9DDE4");
            hashes[6].SetHex ("76DCC77C4027309B5A91AD164083264D70B77B5E43E08AEDA5EBF94361143615");
            hashes[7].SetHex ("DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA02BBE7230E5");

            SHAMap map (SHAMapType::FREE, f);
            if (! backed)
                map.setUnbacked ();

            expect (map.getHash() == zero, "bad initial empty map hash");
            for (int i = 0; i < keys.size(); ++i)
            {
                SHAMapItem item (keys[i], IntToVUC (i));
                expect (map.addItem (std::move(item), true, false), "unable to add item");
                expect (map.getHash().as_uint256() == hashes[i], "bad buildup map hash");
            }
            for (int i = keys.size() - 1; i >= 0; --i)
            {
                expect (map.getHash().as_uint256() == hashes[i], "bad teardown hash");
                expect (map.delItem (keys[i]), "unable to remove item");
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
            SHAMap map{SHAMapType::FREE, f};
            if (! backed)
                map.setUnbacked ();
            for (auto const& k : keys)
                map.addItem(SHAMapItem{k, IntToVUC(0)}, true, false);

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
