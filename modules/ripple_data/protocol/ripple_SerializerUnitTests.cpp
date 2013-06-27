//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

BOOST_AUTO_TEST_SUITE (Serializer_suite)

BOOST_AUTO_TEST_CASE ( Serializer_PrefixHash_test )
{
    using namespace ripple;

    Serializer s1;
    s1.add32 (3);
    s1.add256 (uint256 ());

    Serializer s2;
    s2.add32 (0x12345600);
    s2.addRaw (s1.peekData ());

    if (s1.getPrefixHash (0x12345600) != s2.getSHA512Half ())
        BOOST_FAIL ("Prefix hash does not work");
}

BOOST_AUTO_TEST_SUITE_END ();
