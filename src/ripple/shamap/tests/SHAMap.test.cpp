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
#include <ripple/shamap/FullBelowCache.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/basics/Blob.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <beast/unit_test/suite.h>
#include <beast/utility/Journal.h>
#include <beast/chrono/manual_clock.h>

namespace ripple {

class SHAMap_test : public beast::unit_test::suite
{
public:
    struct Handler
    {
        void operator()(std::uint32_t refNum) const
        {
            throw std::runtime_error("missing node");
        }
    };

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

        beast::manual_clock <std::chrono::steady_clock> clock;  // manual advance clock
        beast::Journal const j;                            // debug journal
        
        FullBelowCache fullBelowCache ("test.full_below", clock);
        TreeNodeCache treeNodeCache ("test.tree_node_cache", 65536, 60, clock, j);
        NodeStore::DummyScheduler scheduler;
        auto db = NodeStore::Manager::instance().make_Database (
            "test", scheduler, j, 0, parseDelimitedKeyValueString("type=memory"));

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        h1.SetHex ("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        h2.SetHex ("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        h3.SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        h4.SetHex ("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        h5.SetHex ("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap (smtFREE, fullBelowCache, treeNodeCache,
            *db, Handler(), beast::Journal());
        SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));
        unexpected (!sMap.addItem (i2, true, false), "no add");
        unexpected (!sMap.addItem (i1, true, false), "no add");

        SHAMapItem::pointer i;
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
        SHAMap::pointer map2 = sMap.snapShot (false);
        unexpected (sMap.getHash () != mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
        unexpected (!sMap.delItem (sMap.peekFirstItem ()->getTag ()), "bad mod");
        unexpected (sMap.getHash () == mapHash, "bad snapshot");
        unexpected (map2->getHash () != mapHash, "bad snapshot");
    }
};

BEAST_DEFINE_TESTSUITE(SHAMap,ripple_app,ripple);

} // ripple
