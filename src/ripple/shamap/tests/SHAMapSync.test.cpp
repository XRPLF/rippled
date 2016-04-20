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
#include <ripple/basics/random.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace tests {

class sync_test : public beast::unit_test::suite
{
public:
    static std::shared_ptr<SHAMapItem> makeRandomAS ()
    {
        Serializer s;

        for (int d = 0; d < 3; ++d)
            s.add32 (rand_int<std::uint32_t>());

        return std::make_shared<SHAMapItem>(
            s.getSHA512Half(), s.peekData ());
    }

    bool confuseMap (SHAMap& map, int count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        SHAMapHash beforeHash = map.getHash ();

        std::list<uint256> items;

        for (int i = 0; i < count; ++i)
        {
            std::shared_ptr<SHAMapItem> item = makeRandomAS ();
            items.push_back (item->key());

            if (!map.addItem (std::move(*item), false, false))
            {
                log << "Unable to add item to map";
                return false;
            }
        }

        for (auto const& item : items)
        {
            if (!map.delItem (item))
            {
                log << "Unable to remove item from map";
                return false;
            }
        }

        if (beforeHash != map.getHash ())
        {
            log << "Hashes do not match " << beforeHash << " " << map.getHash ();
            return false;
        }

        return true;
    }

    void run ()
    {
        beast::Journal const j; // debug journal
        TestFamily f(j);
        SHAMap source (SHAMapType::FREE, f);
        SHAMap destination (SHAMapType::FREE, f);

        int items = 10000;
        for (int i = 0; i < items; ++i)
            source.addItem (std::move(*makeRandomAS ()), false, false);

        expect (confuseMap (source, 500), "ConfuseMap");

        source.setImmutable ();

        destination.setSynching ();

        {
            std::vector<SHAMapNodeID> gotNodeIDs;
            std::vector<Blob> gotNodes;

            expect (source.getNodeFat (
                SHAMapNodeID (),
                gotNodeIDs,
                gotNodes,
                rand_bool(),
                rand_int(2)), "getNodeFat (1)");

            unexpected (gotNodes.size () < 1, "NodeSize");

            expect (destination.addRootNode (
                source.getHash(),
                *gotNodes.begin (),
                snfWIRE,
                nullptr).isGood(), "addRootNode");
        }

        do
        {
            f.clock().advance(std::chrono::seconds(1));

            // get the list of nodes we know we need
            auto nodesMissing = destination.getMissingNodes (2048, nullptr);

            if (nodesMissing.empty ())
                break;

            // get as many nodes as possible based on this information
            std::vector<SHAMapNodeID> gotNodeIDs;
            std::vector<Blob> gotNodes;

            for (auto& it : nodesMissing)
            {
                expect (source.getNodeFat (
                    it.first,
                    gotNodeIDs,
                    gotNodes,
                    rand_bool(),
                    rand_int(2)), "getNodeFat (2)");
            }

            expect (gotNodeIDs.size () == gotNodes.size (), "Size mismatch");
            expect (!gotNodeIDs.empty (), "Didn't get NodeID");

            for (std::size_t i = 0; i < gotNodeIDs.size(); ++i)
            {
                expect (
                    destination.addKnownNode (
                        gotNodeIDs[i],
                        gotNodes[i],
                        nullptr).isGood (), "addKnownNode");
            }
        }
        while (true);

        destination.clearSynching ();

        expect (source.deepCompare (destination), "Deep Compare");
    }
};

BEAST_DEFINE_TESTSUITE(sync,shamap,ripple);

} // tests
} // ripple
