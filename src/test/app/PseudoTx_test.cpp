//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.
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

#include <ripple/app/tx/apply.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <string>
#include <test/jtx.h>
#include <vector>

namespace ripple {
namespace test {

struct PseudoTx_test : public beast::unit_test::suite
{
    std::vector<STTx>
    getPseudoTxs(Rules const& rules, std::uint32_t seq)
    {
        std::vector<STTx> res;

        res.emplace_back(STTx(ttFEE, [&](auto& obj) {
            obj[sfAccount] = AccountID();
            obj[sfLedgerSequence] = seq;
            if (rules.enabled(featureXRPFees))
            {
                obj[sfBaseFeeDrops] = XRPAmount{0};
                obj[sfReserveBaseDrops] = XRPAmount{0};
                obj[sfReserveIncrementDrops] = XRPAmount{0};
            }
            else
            {
                obj[sfBaseFee] = 0;
                obj[sfReserveBase] = 0;
                obj[sfReserveIncrement] = 0;
                obj[sfReferenceFeeUnits] = 0;
            }
        }));

        res.emplace_back(STTx(ttAMENDMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID());
            obj.setFieldH256(sfAmendment, uint256(2));
            obj.setFieldU32(sfLedgerSequence, seq);
        }));

        return res;
    }

    std::vector<STTx>
    getRealTxs()
    {
        std::vector<STTx> res;

        res.emplace_back(STTx(
            ttACCOUNT_SET, [&](auto& obj) { obj[sfAccount] = AccountID(1); }));

        res.emplace_back(STTx(ttPAYMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID(2));
            obj.setAccountID(sfDestination, AccountID(3));
        }));

        return res;
    }

    void
    testPrevented(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);

        for (auto const& stx :
             getPseudoTxs(env.closed()->rules(), env.closed()->seq() + 1))
        {
            std::string reason;
            BEAST_EXPECT(isPseudoTx(stx));
            BEAST_EXPECT(!passesLocalChecks(stx, reason));
            BEAST_EXPECT(reason == "Cannot submit pseudo transactions.");
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    auto const result =
                        ripple::apply(env.app(), view, stx, tapNONE, j);
                    BEAST_EXPECT(!result.second && result.first == temINVALID);
                    return result.second;
                });
        }
    }

    void
    testAllowed()
    {
        for (auto const& stx : getRealTxs())
        {
            std::string reason;
            BEAST_EXPECT(!isPseudoTx(stx));
            BEAST_EXPECT(passesLocalChecks(stx, reason));
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const xrpFees{featureXRPFees};

        testPrevented(all - featureXRPFees);
        testPrevented(all);
        testAllowed();
    }
};

BEAST_DEFINE_TESTSUITE(PseudoTx, app, ripple);

}  // namespace test
}  // namespace ripple
