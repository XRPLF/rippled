//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================
BOOST_AUTO_TEST_SUITE (LoadManager_test)

BOOST_AUTO_TEST_CASE (LoadFeeTrack_test)
{
    WriteLog (lsDEBUG, LoadManager) << "Running load fee track test";

    Config d; // get a default configuration object
    LoadFeeTrack l;

    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (10000, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 10000);
    BOOST_REQUIRE_EQUAL (l.scaleFeeLoad (10000, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE, false), 10000);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (1, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 1);
    BOOST_REQUIRE_EQUAL (l.scaleFeeLoad (1, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE, false), 1);

    // Check new default fee values give same fees as old defaults
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_DEFAULT, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 10);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_ACCOUNT_RESERVE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 200 * SYSTEM_CURRENCY_PARTS);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_OWNER_RESERVE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 50 * SYSTEM_CURRENCY_PARTS);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_NICKNAME_CREATE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 1000);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_OFFER, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 10);
    BOOST_REQUIRE_EQUAL (l.scaleFeeBase (d.FEE_CONTRACT_OPERATION, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE), 1);

}

BOOST_AUTO_TEST_SUITE_END ()
