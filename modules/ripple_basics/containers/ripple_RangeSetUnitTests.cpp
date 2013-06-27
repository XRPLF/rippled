//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================
BOOST_AUTO_TEST_SUITE (RangeSet_suite)

BOOST_AUTO_TEST_CASE (RangeSet_test)
{
    WriteLog (lsTRACE, RangeSet) << "RangeSet test begins";

    RangeSet r1, r2;

    r1.setRange (1, 10);
    r1.clearValue (5);
    r1.setRange (11, 20);

    r2.setRange (1, 4);
    r2.setRange (6, 10);
    r2.setRange (10, 20);

    if (r1.hasValue (5))     BOOST_FAIL ("RangeSet fail");

    if (!r2.hasValue (9))    BOOST_FAIL ("RangeSet fail");

    // TODO: Traverse functions must be tested

    WriteLog (lsTRACE, RangeSet) << "RangeSet test complete";
}

BOOST_AUTO_TEST_SUITE_END ()
