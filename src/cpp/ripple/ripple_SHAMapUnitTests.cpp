//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Rename this to createFilledVector and pass an unsigned char, tidy up
//
static Blob IntToVUC (int v)
{
    Blob vuc;

    for (int i = 0; i < 32; ++i)
        vuc.push_back (static_cast<unsigned char> (v));

    return vuc;
}

BOOST_AUTO_TEST_SUITE (SHAMap_suite)

BOOST_AUTO_TEST_CASE ( SHAMap_test )
{
    // h3 and h4 differ only in the leaf, same terminal node (level 19)
    WriteLog (lsTRACE, SHAMap) << "SHAMap test";
    uint256 h1, h2, h3, h4, h5;
    h1.SetHex ("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
    h2.SetHex ("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
    h3.SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
    h4.SetHex ("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
    h5.SetHex ("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

    SHAMap sMap (smtFREE);
    SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));

    if (!sMap.addItem (i2, true, false)) BOOST_FAIL ("no add");

    if (!sMap.addItem (i1, true, false)) BOOST_FAIL ("no add");

    SHAMapItem::pointer i;

    i = sMap.peekFirstItem ();

    if (!i || (*i != i1)) BOOST_FAIL ("bad traverse");

    i = sMap.peekNextItem (i->getTag ());

    if (!i || (*i != i2)) BOOST_FAIL ("bad traverse");

    i = sMap.peekNextItem (i->getTag ());

    if (i) BOOST_FAIL ("bad traverse");

    sMap.addItem (i4, true, false);
    sMap.delItem (i2.getTag ());
    sMap.addItem (i3, true, false);

    i = sMap.peekFirstItem ();

    if (!i || (*i != i1)) BOOST_FAIL ("bad traverse");

    i = sMap.peekNextItem (i->getTag ());

    if (!i || (*i != i3)) BOOST_FAIL ("bad traverse");

    i = sMap.peekNextItem (i->getTag ());

    if (!i || (*i != i4)) BOOST_FAIL ("bad traverse");

    i = sMap.peekNextItem (i->getTag ());

    if (i) BOOST_FAIL ("bad traverse");

    WriteLog (lsTRACE, SHAMap) << "SHAMap snap test";
    uint256 mapHash = sMap.getHash ();
    SHAMap::pointer map2 = sMap.snapShot (false);

    if (sMap.getHash () != mapHash) BOOST_FAIL ("bad snapshot");

    if (map2->getHash () != mapHash) BOOST_FAIL ("bad snapshot");

    if (!sMap.delItem (sMap.peekFirstItem ()->getTag ())) BOOST_FAIL ("bad mod");

    if (sMap.getHash () == mapHash) BOOST_FAIL ("bad snapshot");

    if (map2->getHash () != mapHash) BOOST_FAIL ("bad snapshot");
}

BOOST_AUTO_TEST_SUITE_END ();
