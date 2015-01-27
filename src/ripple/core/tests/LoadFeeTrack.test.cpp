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
#include <ripple/core/impl/LoadFeeTrackImp.h>
#include <ripple/core/Config.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class LoadFeeTrack_test : public beast::unit_test::suite
{
public:

    int doLedger (LoadFeeTrackImp& l, int max_fee, int max_txns)
    {
        // Attempts to apply as many transactions as possible up to the maximum
        // fee. Returns how many got in before consensus and how many got in
        // after

        int count = 0;

        // Accept transactions into open ledger
        std::vector <int> feesPaid;
        do
        {
            int fee = l.scaleTxnFee(l.getLoadBase());
            if (fee > max_fee)
            {
                log << "Unwilling to pay " << fee;
                break;
            }
            ++count;
            feesPaid.push_back(fee);
            l.onTx(fee);
        }
        while (1);

        l.onLedger (0, feesPaid, count <= max_txns);
        return count;
    }

    void run ()
    {
        LoadFeeTrackImp l (false);

        std::uint64_t fee_default = 10;
        std::uint64_t fee_base = 10;
        std::uint64_t fee_account_reserve = 200000000;

        expect (l.scaleFeeBase (10000, fee_default, fee_base) == 10000,
            "scaleFeeBase(10k)");

        expect (l.scaleFeeLoad (10000, fee_default, fee_base, false) == 10000,
            "scaleFeeLoad(10k)");

        expect (l.scaleFeeBase (1, fee_default, fee_base) == 1,
            "scaleFeeBase(1)");

        expect (l.scaleFeeLoad (1, fee_default, fee_base, false) == 1,
            "scaleFeeLoad(1)");

        expect (l.scaleFeeBase (fee_default, fee_default, fee_base) == 10,
            "scaleFeeBase(default)");

        expect (l.scaleFeeBase (fee_account_reserve, fee_default, fee_base) == 200 * SYSTEM_CURRENCY_PARTS,
            "scaleFeeBase(reserve)");

        // Check transaction-based fee escalation
        std::vector<int> expected({8, 12, 17, 25, 36, 51, 70});
        for (int i = 0; i < 40; ++i)
        {
            int count = doLedger (l, 256000, 100);
            log << "Ledger: " << i + 1 << ", Count: " << count;
            expect(((i < expected.size()) && (count == expected[i]))
                || (count == expected.back()));
        }
    }
};

BEAST_DEFINE_TESTSUITE(LoadFeeTrack,ripple_core,ripple);

} // ripple
