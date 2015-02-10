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
namespace shamap {
namespace tests {

inline bool operator== (SHAMapItem const& a, SHAMapItem const& b) { return a.getTag() == b.getTag(); }
inline bool operator!= (SHAMapItem const& a, SHAMapItem const& b) { return a.getTag() != b.getTag(); }
inline bool operator== (SHAMapItem const& a, uint256 const& b) { return a.getTag() == b; }
inline bool operator!= (SHAMapItem const& a, uint256 const& b) { return a.getTag() != b; }

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
        testcase ("add/traverse");

        beast::Journal const j;                            // debug journal
        
        tests::TestFamily f(j);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        h1.SetHex ("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        h2.SetHex ("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        h3.SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        h4.SetHex ("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        h5.SetHex ("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap (SHAMapType::FREE, f, beast::Journal());
        SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));
        unexpected (!sMap.addItem (i2, true, false), "no add");
        unexpected (!sMap.addItem (i1, true, false), "no add");

        std::shared_ptr<SHAMapItem> i;
        i = sMap.peekFirstItem ();
        unexpected (!i || (*i != i1), "bad traverse");
        i = sMap.peekNextItem (i->getTag ());
        unexpected (!i || (*i != i2), "bad traverse");
        i = sMap.peekNextItem (i->getTag ());
        unexpected (i, "bad traverse");
        sMap.addItem (i4, true, false);
        sMap.delItem (i2.getTag ());
        sMap.addItem (i3, true, false);
        i = sMap.peekFirstItem ();
        unexpected (!i || (*i != i1), "bad traverse");
        i = sMap.peekNextItem (i->getTag ());
        unexpected (!i || (*i != i3), "bad traverse");
        i = sMap.peekNextItem (i->getTag ());
        unexpected (!i || (*i != i4), "bad traverse");
        i = sMap.peekNextItem (i->getTag ());
        unexpected (i, "bad traverse");

        testcase ("snapshot");
        uint256 mapHash = sMap.getHash ();
        std::shared_ptr<SHAMap> map2 = sMap.snapShot (false);
        unexpected (sMap.getHash () != mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
        unexpected (!sMap.delItem (sMap.peekFirstItem ()->getTag ()), "bad mod");
        unexpected (sMap.getHash () == mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
    }
};

BEAST_DEFINE_TESTSUITE(SHAMap,ripple_app,ripple);

} // tests
} // shamap
} // ripple
