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
#include <ripple/rpc/GRPCHandlers.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {
namespace jtx {

/** Convenience class to test AMM functionality.
 */
class AMM
{
    Env& env_;
    Account const creatorAccount_;
    uint256 ammID_;
    AccountID ammAccount_;
    Issue lptIssue_;
    STAmount asset1_;
    STAmount asset2_;
    std::optional<ter> ter_;
    bool log_ = false;

    void
    create(
        std::uint32_t tfee = 0,
        std::optional<std::uint32_t> flags = std::nullopt,
        std::optional<jtx::seq> seq = std::nullopt);

    void
    deposit(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<jtx::seq> const& seq = std::nullopt);

    void
    withdraw(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter = std::nullopt);

    void
    log(bool log)
    {
        log_ = log;
    }

    bool
    expectAmmInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        Json::Value const& jv) const;

public:
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        bool log = false,
        std::uint32_t tfee = 0,
        std::optional<std::uint32_t> flags = std::nullopt,
        std::optional<jtx::seq> seq = std::nullopt,
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
    ammRpcInfo(
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledgerIndex = std::nullopt,
        std::optional<uint256> const& ammID = std::nullopt,
        bool useAssets = false) const;

    /** Verify the AMM balances.
     */
    bool
    expectBalances(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& lpt,
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledger_index = std::nullopt) const;

    bool
    expectLPTokens(AccountID const& account, IOUAmount const& tokens) const;

    bool
    expectAuctionSlot(
        std::uint32_t fee,
        std::uint32_t timeInterval,
        IOUAmount const& price,
        std::optional<std::string> const& ledger_index = std::nullopt) const;

    bool
    expectTradingFee(std::uint16_t fee) const;

    bool
    expectAmmRpcInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledger_index = std::nullopt) const;

    bool
    ammExists() const;

    void
    deposit(
        std::optional<Account> const& account,
        std::uint64_t tokens,
        std::optional<STAmount> const& asset1InDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    deposit(
        std::optional<Account> const& account,
        STAmount const& asset1InDetails,
        std::optional<STAmount> const& asset2InAmount = std::nullopt,
        std::optional<STAmount> const& maxEP = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    deposit(
        std::optional<Account> const& account,
        std::optional<std::uint64_t> tokens,
        std::optional<STAmount> const& asset1In,
        std::optional<STAmount> const& asset2In,
        std::optional<STAmount> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter = std::nullopt);

    void
    withdraw(
        std::optional<Account> const& account,
        std::optional<std::uint64_t> const& tokens,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    withdrawAll(
        std::optional<Account> const& account,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt)
    {
        withdraw(
            account,
            std::nullopt,
            asset1OutDetails,
            tfAMMWithdrawAll,
            std::nullopt);
    }

    void
    withdraw(
        std::optional<Account> const& account,
        STAmount const& asset1Out,
        std::optional<STAmount> const& asset2Out = std::nullopt,
        std::optional<IOUAmount> const& maxEP = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    withdraw(
        std::optional<Account> const& account,
        std::optional<std::uint64_t> const& tokens,
        std::optional<STAmount> const& asset1Out,
        std::optional<STAmount> const& asset2Out,
        std::optional<IOUAmount> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter = std::nullopt);

    void
    vote(
        std::optional<Account> const& account,
        std::uint32_t feeVal,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    bid(std::optional<Account> const& account,
        std::optional<std::uint64_t> const& minSlotPrice = std::nullopt,
        std::optional<std::uint64_t> const& maxSlotPrice = std::nullopt,
        std::vector<Account> const& authAccounts = {},
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    AccountID const&
    ammAccount() const
    {
        return ammAccount_;
    }

    uint256
    ammID() const
    {
        return ammID_;
    }

    Issue
    lptIssue() const
    {
        return lptIssue_;
    }
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
