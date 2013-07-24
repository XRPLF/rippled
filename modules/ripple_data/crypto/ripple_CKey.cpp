//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.


// VFALCO TODO move inlined stuff from CKey into here

class CKeyTests : public UnitTest
{
public:
    CKeyTests () : UnitTest ("CKey", "ripple")
    {
    }

    void runTest ()
    {
        beginTest ("determinism");

        uint128 seed1, seed2;
        seed1.SetHex ("71ED064155FFADFA38782C5E0158CB26");
        seed2.SetHex ("CF0C3BE4485961858C4198515AE5B965");
        CKey root1 (seed1), root2 (seed2);

        uint256 priv1, priv2;
        root1.GetPrivateKeyU (priv1);
        root2.GetPrivateKeyU (priv2);

        if (priv1.GetHex () != "7CFBA64F771E93E817E15039215430B53F7401C34931D111EAB3510B22DBB0D8")
            fail ("Incorrect private key for generator");

        if (priv2.GetHex () != "98BC2EACB26EB021D1A6293C044D88BA2F0B6729A2772DEEBF2E21A263C1740B")
            fail ("Incorrect private key for generator");

        RippleAddress nSeed;
        nSeed.setSeed (seed1);

        if (nSeed.humanSeed () != "shHM53KPZ87Gwdqarm1bAmPeXg8Tn")
            fail ("Incorrect human seed");

        if (nSeed.humanSeed1751 () != "MAD BODY ACE MINT OKAY HUB WHAT DATA SACK FLAT DANA MATH")
            fail ("Incorrect 1751 seed");
    }
};

static CKeyTests cKeyTests;
