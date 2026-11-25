#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xrpl::test {

class AMMInfo_test : public jtx::AMMTestBase
{
public:
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;

        Account const bogie("bogie");
        enum class TestAccount { None, Alice, Bogie };
        auto accountId = [&](AMM const& ammAlice, TestAccount v) -> std::optional<AccountID> {
            if (v == TestAccount::Alice)
            {
                return ammAlice.ammAccount();
            }
            if (v == TestAccount::Bogie)
            {
                return bogie;
            }

            return std::nullopt;
        };

        // Invalid tokens pair
        testAMM([&](AMM& ammAlice, Env&) {
            Account const gw("gw");
            auto const usd = gw["USD"];
            auto const jv = ammAlice.ammRpcInfo({}, {}, usd, usd);
            BEAST_EXPECT(jv[jss::error] == "actNotFound");
            BEAST_EXPECT(jv[jss::error_code] == RpcActNotFound);
            BEAST_EXPECT(jv[jss::error_message] == "Account not found.");
        });

        // Invalid LP account id
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv = ammAlice.ammRpcInfo(bogie.id());
            BEAST_EXPECT(jv[jss::error] == "actMalformed");
            BEAST_EXPECT(jv[jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        // Account is not a string
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv =
                ammAlice.ammRpcInfo(json::Value{42}, std::nullopt, XRP, USD, std::nullopt, true, 3);
            BEAST_EXPECT(jv[jss::error] == "actMalformed");
            BEAST_EXPECT(jv[jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        // AMM Account is not a string
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv = ammAlice.ammRpcInfo(
                json::Value{to_string(ammAlice.ammAccount())},
                std::nullopt,
                XRP,
                USD,
                json::Value{42},
                false,
                3);
            BEAST_EXPECT(jv[jss::error] == "actMalformed");
            BEAST_EXPECT(jv[jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        std::vector<std::tuple<std::optional<Issue>, std::optional<Issue>, TestAccount, bool>> const
            invalidParams = {
                {xrpIssue(), std::nullopt, TestAccount::None, false},
                {std::nullopt, USD, TestAccount::None, false},
                {xrpIssue(), std::nullopt, TestAccount::Alice, false},
                {std::nullopt, USD, TestAccount::Alice, false},
                {xrpIssue(), USD, TestAccount::Alice, false},
                {std::nullopt, std::nullopt, TestAccount::None, true},
        };

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
                BEAST_EXPECT(jv[jss::error] == "invalidParams");
                BEAST_EXPECT(jv[jss::error_code] == RpcInvalidParams);
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
                BEAST_EXPECT(jv[jss::error] == "invalidParams");
                BEAST_EXPECT(jv[jss::error_code] == RpcInvalidParams);
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
                BEAST_EXPECT(jv[jss::error] == "actMalformed");
                BEAST_EXPECT(jv[jss::error_code] == RpcActMalformed);
                BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
            }
        });

        // Invalid AMM account id
        testAMM([&](AMM& ammAlice, Env&) {
            auto const jv = ammAlice.ammRpcInfo(
                std::nullopt, std::nullopt, std::nullopt, std::nullopt, bogie.id());
            BEAST_EXPECT(jv[jss::error] == "actMalformed");
            BEAST_EXPECT(jv[jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(jv[jss::error_message] == "Account malformed.");
        });

        std::vector<std::tuple<std::optional<Issue>, std::optional<Issue>, TestAccount, bool>> const
            invalidParamsBadAccount = {
                {xrpIssue(), std::nullopt, TestAccount::None, false},
                {std::nullopt, USD, TestAccount::None, false},
                {xrpIssue(), std::nullopt, TestAccount::Bogie, false},
                {std::nullopt, USD, TestAccount::Bogie, false},
                {xrpIssue(), USD, TestAccount::Bogie, false},
                {std::nullopt, std::nullopt, TestAccount::None, true},
        };

        // Invalid parameters *and* invalid AMM account, default API version
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] : invalidParamsBadAccount)
            {
                auto const jv = ammAlice.ammRpcInfo(
                    std::nullopt,
                    std::nullopt,
                    iss1,
                    iss2,
                    accountId(ammAlice, acct),
                    ignoreParams);
                BEAST_EXPECT(jv[jss::error] == "invalidParams");
                BEAST_EXPECT(jv[jss::error_code] == RpcInvalidParams);
                BEAST_EXPECT(jv[jss::error_message] == "Invalid parameters.");
            }
        });

        // Invalid parameters *and* invalid AMM account, API version 3
        testAMM([&](AMM& ammAlice, Env&) {
            for (auto const& [iss1, iss2, acct, ignoreParams] : invalidParamsBadAccount)
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
                    jv[jss::error] ==
                    (acct == TestAccount::Bogie ? std::string("actMalformed")
                                                : std::string("invalidParams")));
                BEAST_EXPECT(
                    jv[jss::error_code] ==
                    (acct == TestAccount::Bogie ? RpcActMalformed : RpcInvalidParams));
                BEAST_EXPECT(
                    jv[jss::error_message] ==
                    (acct == TestAccount::Bogie ? std::string("Account malformed.")
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
                XRP(10000),
                USD(10000),
                IOUAmount{10000000, 0},
                std::nullopt,
                std::nullopt,
                ammAlice.ammAccount()));
        });

        {
            Env env{*this};
            env.fund(XRP(1'000), gw_);
            MPTTester mptTester(env, gw_, {.fund = false});
            mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            MPTTester mptTester1(env, gw_, {.fund = false});
            mptTester1.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            auto const mpt1 = mptTester1["MPT"];
            std::vector<std::tuple<PrettyAmount, PrettyAmount, IOUAmount>> pools = {
                {XRP(100), mpt(100), IOUAmount{100'000}},
                {USD(100), mpt(100), IOUAmount{100}},
                {mpt(100), mpt1(100), IOUAmount{100}},
            };
            for (auto& pool : pools)
            {
                AMM const amm(env, gw_, std::get<0>(pool), std::get<1>(pool));
                BEAST_EXPECT(amm.expectAmmRpcInfo(
                    std::get<0>(pool),
                    std::get<1>(pool),
                    std::get<2>(pool),
                    std::nullopt,
                    std::nullopt,
                    amm.ammAccount()));
            }
        }
    }

    void
    testVoteAndBid(FeatureBitset features)
    {
        testcase("Vote and Bid");

        using namespace jtx;
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                BEAST_EXPECT(
                    ammAlice.expectAmmRpcInfo(XRP(10000), USD(10000), IOUAmount{10000000, 0}));
                std::unordered_map<std::string, std::uint16_t> votes;
                votes.insert({alice_.human(), 0});
                for (int i = 0; i < 7; ++i)
                {
                    Account a(std::to_string(i));
                    votes.insert({a.human(), 50 * (i + 1)});
                    if (!features[fixAMMv1_3])
                    {
                        fund(env, gw_, {a}, {USD(10000)}, Fund::Acct);
                    }
                    else
                    {
                        fund(env, gw_, {a}, {USD(10001)}, Fund::Acct);
                    }
                    ammAlice.deposit(a, 10000000);
                    ammAlice.vote(a, 50 * (i + 1));
                }
                BEAST_EXPECT(ammAlice.expectTradingFee(175));
                Account ed("ed");
                Account bill("bill");
                env.fund(XRP(1000), bob_, ed, bill);
                env(ammAlice.bid({.bidMin = 100, .authAccounts = {carol_, bob_, ed, bill}}));
                if (!features[fixAMMv1_3])
                {
                    BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                        XRP(80000),
                        USD(80000),
                        IOUAmount{79994400},
                        std::nullopt,
                        std::nullopt,
                        ammAlice.ammAccount()));
                }
                else
                {
                    BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                        XRPAmount(80000000005),
                        STAmount{USD, UINT64_C(80'000'00000000005), -11},
                        IOUAmount{79994400},
                        std::nullopt,
                        std::nullopt,
                        ammAlice.ammAccount()));
                }
                for (auto i = 0; i < 2; ++i)
                {
                    std::unordered_set<std::string> authAccounts = {
                        carol_.human(),
                        bob_.human(),
                        ed.human(),
                        bill.human(),
                    };
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
                        auto const votesCopy = votes;
                        for (std::uint8_t i = 0; i < 8; ++i)
                        {
                            if (!BEAST_EXPECT(
                                    votes[voteSlots[i][jss::account].asString()] ==
                                        voteSlots[i][jss::trading_fee].asUInt() &&
                                    voteSlots[i][jss::vote_weight].asUInt() == 12500))
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
                                    auctionSlot[jss::auth_accounts][i][jss::account].asString())))
                                return;
                            authAccounts.erase(
                                auctionSlot[jss::auth_accounts][i][jss::account].asString());
                        }
                        if (!BEAST_EXPECT(authAccounts.empty()))
                            return;
                        BEAST_EXPECT(
                            auctionSlot[jss::account].asString() == alice_.human() &&
                            auctionSlot[jss::discounted_fee].asUInt() == 17 &&
                            auctionSlot[jss::price][jss::value].asString() == "5600" &&
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
            },
            std::nullopt,
            0,
            std::nullopt,
            {features});
    }

    void
    testFreeze()
    {
        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw_, asfGlobalFreeze));
            env.close();
            auto const test = [&](bool freeze) {
                auto const info = ammAlice.ammRpcInfo();
                BEAST_EXPECT(info[jss::amm][jss::asset2_frozen].asBool() == freeze);
            };
            test(true);
            env(fclear(gw_, asfGlobalFreeze));
            env.close();
            test(false);
        });
    }

    void
    testInvalidAmmField()
    {
        using namespace jtx;
        testcase("Invalid amm field");

        testAMM([&](AMM& amm, Env&) {
            auto const resp = amm.ammRpcInfo(
                std::nullopt, jss::validated.cStr(), std::nullopt, std::nullopt, gw_);
            BEAST_EXPECT(resp[jss::error] == "actNotFound");
            BEAST_EXPECT(resp[jss::error_code] == RpcActNotFound);
            BEAST_EXPECT(resp[jss::error_message] == "Account not found.");
        });
    }

    void
    testErrorsCarryLedgerFields()
    {
        using namespace jtx;
        testcase("Errors carry the ledger fields");

        testAMM([&](AMM& ammAlice, Env& env) {
            Account const bogie("bogie");
            json::Value xrp;
            xrp[jss::currency] = "XRP";
            json::Value const usd = toJson(USD.issue());
            json::Value usdNoIssuer;
            usdNoIssuer[jss::currency] = "USD";
            json::Value badMpt;
            badMpt[jss::mpt_issuance_id] = "NOTHEX";

            auto const withAssets = [](json::Value const& asset, json::Value const& asset2) {
                json::Value params;
                params[jss::asset] = asset;
                params[jss::asset2] = asset2;
                return params;
            };
            auto const expectError = [&](json::Value const& params,
                                         std::string const& error,
                                         std::string const& message,
                                         unsigned apiVersion = 1) {
                auto const jv =
                    env.rpc(apiVersion, "json", "amm_info", to_string(params))[jss::result];
                BEAST_EXPECT(jv[jss::error] == error);
                BEAST_EXPECT(jv[jss::error_message] == message);
                BEAST_EXPECT(jv.isMember(jss::ledger_current_index));
                BEAST_EXPECT(jv.isMember(jss::validated));
            };

            expectError(withAssets("XRP", usd), "issueMalformed", "Issue is malformed.");
            expectError(withAssets(xrp, usdNoIssuer), "issueMalformed", "Issue is malformed.");
            expectError(withAssets(xrp, badMpt), "issueMalformed", "Issue is malformed.");
            expectError(withAssets(json::Value{}, usd), "issueMalformed", "Issue is malformed.");

            for (auto const& account : {json::Value{42}, json::Value{}, json::Value{bogie.human()}})
            {
                auto params = withAssets(xrp, usd);
                params[jss::account] = account;
                expectError(params, "actMalformed", "Account malformed.");
            }

            json::Value params;
            params[jss::amm_account] = bogie.human();
            expectError(params, "actMalformed", "Account malformed.");

            params = json::Value{};
            params[jss::asset] = xrp;
            expectError(params, "invalidParams", "Invalid parameters.");

            params[jss::account] = bogie.human();
            expectError(params, "invalidParams", "Invalid parameters.");
            expectError(params, "actMalformed", "Account malformed.", 3);

            expectError(withAssets(usd, usd), "actNotFound", "Account not found.");

            params = withAssets(xrp, usd);
            params[jss::ledger_index] = 1000;
            auto const jv = env.rpc("json", "amm_info", to_string(params))[jss::result];
            BEAST_EXPECT(jv[jss::error] == "lgrNotFound");
            BEAST_EXPECT(!jv.isMember(jss::ledger_current_index));
            BEAST_EXPECT(!jv.isMember(jss::validated));

            BEAST_EXPECT(ammAlice.ammRpcInfo().isMember(jss::amm));
        });
    }

    void
    run() override
    {
        using namespace jtx;
        auto const all = testableAmendments();
        testErrors();
        testSimpleRpc();
        testVoteAndBid(all);
        testVoteAndBid(all - fixAMMv1_3);
        testFreeze();
        testInvalidAmmField();
        testErrorsCarryLedgerFields();
    }
};

BEAST_DEFINE_TESTSUITE(AMMInfo, rpc, xrpl);

}  // namespace xrpl::test
