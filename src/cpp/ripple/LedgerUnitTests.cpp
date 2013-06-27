//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

BOOST_AUTO_TEST_SUITE (quality)

BOOST_AUTO_TEST_CASE ( getquality )
{
    uint256 uBig ("D2DC44E5DC189318DB36EF87D2104CDF0A0FE3A4B698BEEE55038D7EA4C68000");

    if (6125895493223874560 != Ledger::getQuality (uBig))
        BOOST_FAIL ("Ledger::getQuality fails.");
}

BOOST_AUTO_TEST_SUITE_END ()
