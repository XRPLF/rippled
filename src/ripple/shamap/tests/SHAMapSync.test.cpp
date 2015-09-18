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
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/tests/common.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/UInt160.h>
#include <beast/unit_test/suite.h>
#include <openssl/rand.h> // DEPRECATED

namespace ripple {
namespace tests {

#ifdef BEAST_DEBUG
//#define SMS_DEBUG
#endif

class sync_test : public beast::unit_test::suite
{
public:
    static std::shared_ptr<SHAMapItem> makeRandomAS ()
    {
        Serializer s;

        for (int d = 0; d < 3; ++d) s.add32 (rand ());

        return std::make_shared<SHAMapItem>(
            s.getSHA512Half(), s.peekData ());
    }

    bool confuseMap (SHAMap& map, int count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        uint256 beforeHash = map.getHash ();

        std::list<uint256> items;

        for (int i = 0; i < count; ++i)
        {
            std::shared_ptr<SHAMapItem> item = makeRandomAS ();
            items.push_back (item->key());

            if (!map.addItem (*item, false, false))
            {
                log <<
                    "Unable to add item to map";
                assert (false);
                return false;
            }
        }

        for (std::list<uint256>::iterator it = items.begin (); it != items.end (); ++it)
        {
            if (!map.delItem (*it))
            {
                log <<
                    "Unable to remove item from map";
                assert (false);
                return false;
            }
        }

        if (beforeHash != map.getHash ())
        {
            log <<
                "Hashes do not match " << beforeHash << " " << map.getHash ();
            assert (false);
            return false;
        }

        return true;
    }

    void run ()
    {
        unsigned int seed;

        // VFALCO DEPRECATED Should use C++11
        RAND_pseudo_bytes (reinterpret_cast<unsigned char*> (&seed), sizeof (seed));
        srand (seed);

        beast::Journal const j;                            // debug journal

        TestFamily f(j);
        SHAMap source (SHAMapType::FREE, f, j);
        SHAMap destination (SHAMapType::FREE, f, j);

        int items = 10000;
        for (int i = 0; i < items; ++i)
            source.addItem (*makeRandomAS (), false, false);

        unexpected (!confuseMap (source, 500), "ConfuseMap");

        source.setImmutable ();

        std::vector<SHAMapNodeID> nodeIDs, gotNodeIDs;
        std::vector< Blob > gotNodes;
        std::vector<uint256> hashes;

        std::vector<SHAMapNodeID>::iterator nodeIDIterator;
        std::vector< Blob >::iterator rawNodeIterator;

        int passes = 0;
        int nodes = 0;

        destination.setSynching ();

        unexpected (!source.getNodeFat (SHAMapNodeID (), nodeIDs, gotNodes,
            (rand () % 2) == 0, rand () % 3),
            "GetNodeFat");

        unexpected (gotNodes.size () < 1, "NodeSize");

        unexpected (!destination.addRootNode (*gotNodes.begin (), snfWIRE, nullptr).isGood(), "AddRootNode");

        nodeIDs.clear ();
        gotNodes.clear ();

#ifdef SMS_DEBUG
        int bytes = 0;
#endif

        do
        {
            f.clock().advance(std::chrono::seconds(1));
            ++passes;
            hashes.clear ();

            // get the list of nodes we know we need
            destination.getMissingNodes (nodeIDs, hashes, 2048, nullptr);

            if (nodeIDs.empty ()) break;

            // get as many nodes as possible based on this information
            for (nodeIDIterator = nodeIDs.begin (); nodeIDIterator != nodeIDs.end (); ++nodeIDIterator)
            {
                if (!source.getNodeFat (*nodeIDIterator, gotNodeIDs, gotNodes,
                    (rand () % 2) == 0, rand () % 3))
                {
                    fail ("GetNodeFat");
                }
                else
                {
                    pass ();
                }
            }

            assert (gotNodeIDs.size () == gotNodes.size ());
            nodeIDs.clear ();
            hashes.clear ();

            if (gotNodeIDs.empty ())
            {
                fail ("Got Node ID");
            }
            else
            {
                pass ();
            }

            for (nodeIDIterator = gotNodeIDs.begin (), rawNodeIterator = gotNodes.begin ();
                    nodeIDIterator != gotNodeIDs.end (); ++nodeIDIterator, ++rawNodeIterator)
            {
                ++nodes;
#ifdef SMS_DEBUG
                bytes += rawNodeIterator->size ();
#endif

                if (!destination.addKnownNode (*nodeIDIterator, *rawNodeIterator, nullptr).isGood ())
                {
                    fail ("AddKnownNode");
                }
                else
                {
                    pass ();
                }
            }

            gotNodeIDs.clear ();
            gotNodes.clear ();
        }
        while (true);

        destination.clearSynching ();

#ifdef SMS_DEBUG
        log << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes, " <<
                                  bytes / 1024 << " KB";
#endif

        if (!source.deepCompare (destination))
        {
            fail ("Deep Compare");
        }
        else
        {
            pass ();
        }

#ifdef SMS_DEBUG
        log << "SHAMapSync test passed: " << items << " items, " <<
            passes << " passes, " << nodes << " nodes";
#endif
    }
};

BEAST_DEFINE_TESTSUITE(sync,shamap,ripple);

} // tests
} // ripple
