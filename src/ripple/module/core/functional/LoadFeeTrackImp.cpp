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

#include <beast/unit_test/suite.h>

namespace ripple {

LoadFeeTrack* LoadFeeTrack::New (beast::Journal journal)
{
    return new LoadFeeTrackImp (journal);
}

//------------------------------------------------------------------------------

class LoadFeeTrack_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        Config d; // get a default configuration object
        LoadFeeTrackImp l;

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

BEAST_DEFINE_TESTSUITE(LoadFeeTrack,ripple_core,ripple);

} // ripple
