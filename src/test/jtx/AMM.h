//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_AMM_H_INCLUDED
#define RIPPLE_TEST_JTX_AMM_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/STAmount.h>
#include <test/jtx/Account.h>

namespace ripple {
namespace test {
namespace jtx {

/** Convenience class to test AMM functionality.
 */
class AMM
{
    Env& env_;
    Account const creatorAccount_;
    AccountID ammAccountID_;
    Issue lptIssue_;
    STAmount asset1_;
    STAmount asset2_;
    std::uint8_t weight1_;
    std::optional<ter> ter_;
    bool log_ = false;

    void
    create(std::uint32_t tfee = 0);

    void
    deposit(std::optional<Account> const& account, Json::Value& jv);

    void
    withdraw(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<ter> const& ter = {});

    void
    swap(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<ter> const& ter = {});

    void
    log(bool log)
    {
        log_ = log;
    }

public:
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        bool log = false,
        std::uint8_t weight1 = 50,
        std::uint32_t tfee = 0,
        std::optional<ter> const& ter = std::nullopt);
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        ter const& ter,
        bool log = false);

    /** Send amm_info RPC command
     */
    std::optional<Json::Value>
    ammInfo(std::optional<Account> const& account = {}) const;

    /** Get AMM pool and tokens balance.
     */
    std::tuple<STAmount, STAmount, STAmount>
    ammBalances(
        std::optional<AccountID> const& account,
        std::optional<Issue> const& issue1 = {},
        std::optional<Issue> const& issue2 = {}) const;

    /** Verify the AMM balances.
     */
    bool
    expectBalances(
        STAmount const& asset1,
        std::optional<STAmount> const& asset2 = std::nullopt,
        std::optional<IOUAmount> const& lpt = std::nullopt,
        std::optional<AccountID> const& account = std::nullopt) const;

    bool
    expectAmmInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        std::optional<Account> const& account = {});

    bool
    accountRootExists() const;

    void
    deposit(
        std::optional<Account> const& account,
        std::uint64_t tokens,
        std::optional<STAmount> const& asset1InDetails = {});

    void
    deposit(
        std::optional<Account> const& account,
        STAmount const& asset1InDetails,
        std::optional<STAmount> const& asset2InAmount = {},
        std::optional<STAmount> const& maxSP = {});

    void
    withdraw(
        std::optional<Account> const& account,
        std::uint64_t tokens,
        std::optional<STAmount> const& asset1OutDetails = {},
        std::optional<ter> const& ter = {});

    void
    withdraw(
        std::optional<Account> const& account,
        STAmount const& asset1Out,
        std::optional<STAmount> const& asset2Out = {},
        std::optional<STAmount> const& maxSP = {},
        std::optional<ter> const& ter = {});

    void
    swapIn(
        std::optional<Account> const& account,
        STAmount const& assetIn,
        std::optional<std::uint16_t> const& slippage = std::nullopt,
        std::optional<STAmount> const& maxSP = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    swapOut(
        std::optional<Account> const& account,
        STAmount const& assetOut,
        std::optional<STAmount> const& maxSP = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    // This could be either swap in/out depending on
    // the combination of asset/slippage/maxSP
    void
    swap(
        std::optional<Account> const& account,
        STAmount const& asset,
        std::uint16_t const& slippage,
        STAmount const& maxSP,
        std::optional<ter> const& ter = std::nullopt);
};

namespace amm {
Json::Value
trust(
    AccountID const& account,
    STAmount const& amount,
    std::uint32_t flags = 0);
Json::Value
pay(Account const& account, AccountID const& to, STAmount const& amount);
}  // namespace amm

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_AMM_H_INCLUDED
