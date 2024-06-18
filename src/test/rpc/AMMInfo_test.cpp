//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>

#include <unordered_map>

namespace ripple {
namespace test {

class AMMInfo_test : public jtx::AMMTestBase
{
public:
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;

        Account const bogie("bogie");
        enum TestAccount { None, Alice, Bogie };
        auto accountId = [&](AMM const& ammAlice,
                             TestAccount v) -> std::optional<AccountID> {
            if (v == Alice)
                return ammAlice.ammAccount();
            else if (v == Bogie)
                return bogie;
            else
                return std::nullopt;
        };

        // Invalid tokens pair
        testAMM([&](AMM& ammAlice, Env&) {
            Account const gw("gw");
            auto const USD = gw["USD"];
            auto const jv =
                ammAlice.ammRpcInfo({}, {}, USD.issue(), USD.issue());
            BEAST_EXPECT(jv[jss::error_message] == "Account not found.");
        });

        // Invalid LP account id
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv = ammAlice.ammRpcInfo(bogie.id());
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        std::vector<std::tuple<
            std::optional<Issue>,
            std::optional<Issue>,
            TestAccount,
            bool>> const invalidParams = {
            {xrpIssue(), std::nullopt, None, false},
            {std::nullopt, USD.issue(), None, false},
            {xrpIssue(), std::nullopt, Alice, false},
            {std::nullopt, USD.issue(), Alice, false},
            {xrpIssue(), USD.issue(), Alice, false},
            {std::nullopt, std::nullopt, None, true}};

        // Invalid parameters
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] : invalidParams)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    std::nullopt,
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams);
                BEAST_EXPECT(jv[jss::error_message] == "Invalid parameters.");
            }
        });

        // Invalid parameters *and* invalid LP account, default API version
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] : invalidParams)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    bogie,  //
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams);
                BEAST_EXPECT(jv[jss::error_message] == "Invalid parameters.");
            }
        });

        // Invalid parameters *and* invalid LP account, API version 3
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] : invalidParams)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    bogie,  //
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams,
                    3);
                BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
            }
        });

        // Invalid AMM account id
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv = ammAlice.ammRpcInfo(
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                bogie.id());
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        std::vector<std::tuple<
            std::optional<Issue>,
            std::optional<Issue>,
            TestAccount,
            bool>> const invalidParamsBadAccount = {
            {xrpIssue(), std::nullopt, None, false},
            {std::nullopt, USD.issue(), None, false},
            {xrpIssue(), std::nullopt, Bogie, false},
            {std::nullopt, USD.issue(), Bogie, false},
            {xrpIssue(), USD.issue(), Bogie, false},
            {std::nullopt, std::nullopt, None, true}};

        // Invalid parameters *and* invalid AMM account, default API version
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] :
                 invalidParamsBadAccount)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    std::nullopt,
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams);
                BEAST_EXPECT(jv[jss::error_message] == "Invalid parameters.");
            }
        });

        // Invalid parameters *and* invalid AMM account, API version 3
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] :
                 invalidParamsBadAccount)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    std::nullopt,
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams,
                    3);
                BEAST_EXPECT(
                    jv[jss::error_message] ==
                    (acct == Bogie ? std::string("Account malformed.")
                                   : std::string("Invalid parameters.")));
            }
        });
    }

    void
    testSimpleRpc()
    {
        testcase("RPC simple");

        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000),
                USD(10000),
                IOUAmount{10000000, 0},
                std::nullopt,
                std::nullopt,
                ammAlice.ammAccount()));
        });
    }

    void
    testVoteAndBid()
    {
        testcase("Vote and Bid");

        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env& env) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            std::unordered_map<std::string, std::uint16_t> votes;
            votes.insert({alice.human(), 0});
            for (int i = 0; i < 7; ++i)
            {
                Account a(std::to_string(i));
                votes.insert({a.human(), 50 * (i + 1)});
                fund(env, gw, {a}, {USD(10000)}, Fund::Acct);
                ammAlice.deposit(a, 10000000);
                ammAlice.vote(a, 50 * (i + 1));
            }
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            Account ed("ed");
            Account bill("bill");
            env.fund(XRP(1000), bob, ed, bill);
            env(ammAlice.bid(
                {.bidMin = 100, .authAccounts = {carol, bob, ed, bill}}));
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(80000),
                USD(80000),
                IOUAmount{79994400},
                std::nullopt,
                std::nullopt,
                ammAlice.ammAccount()));
            for (auto i = 0; i < 2; ++i)
            {
                std::unordered_set<std::string> authAccounts = {
                    carol.human(), bob.human(), ed.human(), bill.human()};
                auto const ammInfo = i ? ammAlice.ammRpcInfo()
                                       : ammAlice.ammRpcInfo(
                                             std::nullopt,
                                             std::nullopt,
                                             std::nullopt,
                                             std::nullopt,
                                             ammAlice.ammAccount());
                auto const& amm = ammInfo[jss::amm];
                try
                {
                    // votes
                    auto const voteSlots = amm[jss::vote_slots];
                    auto votesCopy = votes;
                    for (std::uint8_t i = 0; i < 8; ++i)
                    {
                        if (!BEAST_EXPECT(
                                votes[voteSlots[i][jss::account].asString()] ==
                                    voteSlots[i][jss::trading_fee].asUInt() &&
                                voteSlots[i][jss::vote_weight].asUInt() ==
                                    12500))
                            return;
                        votes.erase(voteSlots[i][jss::account].asString());
                    }
                    if (!BEAST_EXPECT(votes.empty()))
                        return;
                    votes = votesCopy;

                    // bid
                    auto const auctionSlot = amm[jss::auction_slot];
                    for (std::uint8_t i = 0; i < 4; ++i)
                    {
                        if (!BEAST_EXPECT(authAccounts.contains(
                                auctionSlot[jss::auth_accounts][i][jss::account]
                                    .asString())))
                            return;
                        authAccounts.erase(
                            auctionSlot[jss::auth_accounts][i][jss::account]
                                .asString());
                    }
                    if (!BEAST_EXPECT(authAccounts.empty()))
                        return;
                    BEAST_EXPECT(
                        auctionSlot[jss::account].asString() == alice.human() &&
                        auctionSlot[jss::discounted_fee].asUInt() == 17 &&
                        auctionSlot[jss::price][jss::value].asString() ==
                            "5600" &&
                        auctionSlot[jss::price][jss::currency].asString() ==
                            to_string(ammAlice.lptIssue().currency) &&
                        auctionSlot[jss::price][jss::issuer].asString() ==
                            to_string(ammAlice.lptIssue().account));
                }
                catch (std::exception const& e)
                {
                    fail(e.what(), __FILE__, __LINE__);
                }
            }
        });
    }

    void
    testFreeze()
    {
        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            auto test = [&](bool freeze) {
                auto const info = ammAlice.ammRpcInfo();
                BEAST_EXPECT(
                    info[jss::amm][jss::asset2_frozen].asBool() == freeze);
            };
            test(true);
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            test(false);
        });
    }

    void
    run() override
    {
        testErrors();
        testSimpleRpc();
        testVoteAndBid();
        testFreeze();
    }
};

BEAST_DEFINE_TESTSUITE(AMMInfo, app, ripple);

}  // namespace test
}  // namespace ripple
