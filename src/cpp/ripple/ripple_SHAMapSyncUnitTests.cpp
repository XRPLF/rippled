//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifdef DEBUG
#define SMS_DEBUG
#endif

namespace ripple
{

static SHAMapItem::pointer makeRandomAS ()
{
    Serializer s;

    for (int d = 0; d < 3; ++d) s.add32 (rand ());

    return boost::make_shared<SHAMapItem> (s.getRIPEMD160 ().to256 (), s.peekData ());
}

static bool confuseMap (SHAMap& map, int count)
{
    // add a bunch of random states to a map, then remove them
    // map should be the same
    uint256 beforeHash = map.getHash ();

    std::list<uint256> items;

    for (int i = 0; i < count; ++i)
    {
        SHAMapItem::pointer item = makeRandomAS ();
        items.push_back (item->getTag ());

        if (!map.addItem (*item, false, false))
        {
            WriteLog (lsFATAL, SHAMap) << "Unable to add item to map";
            return false;
        }
    }

    for (std::list<uint256>::iterator it = items.begin (); it != items.end (); ++it)
    {
        if (!map.delItem (*it))
        {
            WriteLog (lsFATAL, SHAMap) << "Unable to remove item from map";
            return false;
        }
    }

    if (beforeHash != map.getHash ())
    {
        WriteLog (lsFATAL, SHAMap) << "Hashes do not match";
        return false;
    }

    return true;
}

}

BOOST_AUTO_TEST_SUITE ( SHAMapSync )

BOOST_AUTO_TEST_CASE ( SHAMapSync_test )
{
    using namespace ripple;

    WriteLog (lsTRACE, SHAMap) << "begin sync test";
    unsigned int seed;
    RAND_pseudo_bytes (reinterpret_cast<unsigned char*> (&seed), sizeof (seed));
    srand (seed);

    WriteLog (lsTRACE, SHAMap) << "Constructing maps";
    SHAMap source (smtFREE), destination (smtFREE);

    // add random data to the source map
    WriteLog (lsTRACE, SHAMap) << "Adding random data";
    int items = 10000;

    for (int i = 0; i < items; ++i)
        source.addItem (*makeRandomAS (), false, false);

    WriteLog (lsTRACE, SHAMap) << "Adding items, then removing them";

    if (!confuseMap (source, 500)) BOOST_FAIL ("ConfuseMap");

    source.setImmutable ();

    WriteLog (lsTRACE, SHAMap) << "SOURCE COMPLETE, SYNCHING";

    std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
    std::list< Blob > gotNodes;
    std::vector<uint256> hashes;

    std::vector<SHAMapNode>::iterator nodeIDIterator;
    std::list< Blob >::iterator rawNodeIterator;

    int passes = 0;
    int nodes = 0;

    destination.setSynching ();

    if (!source.getNodeFat (SHAMapNode (), nodeIDs, gotNodes, (rand () % 2) == 0, (rand () % 2) == 0))
    {
        WriteLog (lsFATAL, SHAMap) << "GetNodeFat(root) fails";
        BOOST_FAIL ("GetNodeFat");
    }

    if (gotNodes.size () < 1)
    {
        WriteLog (lsFATAL, SHAMap) << "Didn't get root node " << gotNodes.size ();
        BOOST_FAIL ("NodeSize");
    }

    if (!destination.addRootNode (*gotNodes.begin (), snfWIRE, NULL))
    {
        WriteLog (lsFATAL, SHAMap) << "AddRootNode fails";
        BOOST_FAIL ("AddRootNode");
    }

    nodeIDs.clear ();
    gotNodes.clear ();

    WriteLog (lsINFO, SHAMap) << "ROOT COMPLETE, INNER SYNCHING";
#ifdef SMS_DEBUG
    int bytes = 0;
#endif

    do
    {
        ++passes;
        hashes.clear ();

        // get the list of nodes we know we need
        destination.getMissingNodes (nodeIDs, hashes, 2048, NULL);

        if (nodeIDs.empty ()) break;

        WriteLog (lsINFO, SHAMap) << nodeIDs.size () << " needed nodes";

        // get as many nodes as possible based on this information
        for (nodeIDIterator = nodeIDs.begin (); nodeIDIterator != nodeIDs.end (); ++nodeIDIterator)
        {
            if (!source.getNodeFat (*nodeIDIterator, gotNodeIDs, gotNodes, (rand () % 2) == 0, (rand () % 2) == 0))
            {
                WriteLog (lsFATAL, SHAMap) << "GetNodeFat fails";
                BOOST_FAIL ("GetNodeFat");
            }
        }

        assert (gotNodeIDs.size () == gotNodes.size ());
        nodeIDs.clear ();
        hashes.clear ();

        if (gotNodeIDs.empty ())
        {
            WriteLog (lsFATAL, SHAMap) << "No nodes gotten";
            BOOST_FAIL ("Got Node ID");
        }

        WriteLog (lsTRACE, SHAMap) << gotNodeIDs.size () << " found nodes";

        for (nodeIDIterator = gotNodeIDs.begin (), rawNodeIterator = gotNodes.begin ();
                nodeIDIterator != gotNodeIDs.end (); ++nodeIDIterator, ++rawNodeIterator)
        {
            ++nodes;
#ifdef SMS_DEBUG
            bytes += rawNodeIterator->size ();
#endif

            if (!destination.addKnownNode (*nodeIDIterator, *rawNodeIterator, NULL))
            {
                WriteLog (lsTRACE, SHAMap) << "AddKnownNode fails";
                BOOST_FAIL ("AddKnownNode");
            }
        }

        gotNodeIDs.clear ();
        gotNodes.clear ();


    }
    while (1);

    destination.clearSynching ();

#ifdef SMS_DEBUG
    WriteLog (lsINFO, SHAMap) << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes, " <<
                              bytes / 1024 << " KB";
#endif

    if (!source.deepCompare (destination))
    {
        WriteLog (lsFATAL, SHAMap) << "DeepCompare fails";
        BOOST_FAIL ("Deep Compare");
    }

#ifdef SMS_DEBUG
    WriteLog (lsINFO, SHAMap) << "SHAMapSync test passed: " << items << " items, " <<
                              passes << " passes, " << nodes << " nodes";
#endif

}

BOOST_AUTO_TEST_SUITE_END ();
