//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

ILoadFeeTrack* ILoadFeeTrack::New ()
{
    return new LoadFeeTrack;
}

//------------------------------------------------------------------------------

class LoadFeeTrackTests : public UnitTest
{
public:
    LoadFeeTrackTests () : UnitTest ("LoadFeeTrack", "ripple")
    {
    }

    void runTest ()
    {
        Config d; // get a default configuration object
        LoadFeeTrack l;

        beginTestCase ("fee scaling");

        expect (l.scaleFeeBase (10000, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 10000);
        expect (l.scaleFeeLoad (10000, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE, false) == 10000);
        expect (l.scaleFeeBase (1, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 1);
        expect (l.scaleFeeLoad (1, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE, false) == 1);

        // Check new default fee values give same fees as old defaults
        expect (l.scaleFeeBase (d.FEE_DEFAULT, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 10);
        expect (l.scaleFeeBase (d.FEE_ACCOUNT_RESERVE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 200 * SYSTEM_CURRENCY_PARTS);
        expect (l.scaleFeeBase (d.FEE_OWNER_RESERVE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 50 * SYSTEM_CURRENCY_PARTS);
        expect (l.scaleFeeBase (d.FEE_NICKNAME_CREATE, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 1000);
        expect (l.scaleFeeBase (d.FEE_OFFER, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 10);
        expect (l.scaleFeeBase (d.FEE_CONTRACT_OPERATION, d.FEE_DEFAULT, d.TRANSACTION_FEE_BASE) == 1);
    }
};


static LoadFeeTrackTests loadFeeTrackTests;
