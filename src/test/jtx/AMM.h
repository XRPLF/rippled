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

#ifndef RIPPLE_TEST_JTX_AMM_H_INCLUDED
#define RIPPLE_TEST_JTX_AMM_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/multisign.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>

namespace ripple {
namespace test {
namespace jtx {

class LPToken
{
    IOUAmount const tokens_;

public:
    LPToken(std::uint64_t tokens) : tokens_(tokens)
    {
    }
    LPToken(IOUAmount tokens) : tokens_(tokens)
    {
    }
    IOUAmount const&
    tokens() const
    {
        return tokens_;
    }
    STAmount
    tokens(Issue const& ammIssue) const
    {
        return STAmount{tokens_, ammIssue};
    }
};

struct CreateArg
{
    bool log = false;
    std::uint16_t tfee = 0;
    std::uint32_t fee = 0;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<jtx::seq> seq = std::nullopt;
    std::optional<jtx::msig> ms = std::nullopt;
    std::optional<ter> err = std::nullopt;
    bool close = true;
};

struct DepositArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<LPToken> tokens = std::nullopt;
    std::optional<STAmount> asset1In = std::nullopt;
    std::optional<STAmount> asset2In = std::nullopt;
    std::optional<STAmount> maxEP = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Issue, Issue>> assets = std::nullopt;
    std::optional<jtx::seq> seq = std::nullopt;
    std::optional<std::uint16_t> tfee = std::nullopt;
    std::optional<ter> err = std::nullopt;
};

struct WithdrawArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<LPToken> tokens = std::nullopt;
    std::optional<STAmount> asset1Out = std::nullopt;
    std::optional<STAmount> asset2Out = std::nullopt;
    std::optional<IOUAmount> maxEP = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Issue, Issue>> assets = std::nullopt;
    std::optional<jtx::seq> seq = std::nullopt;
    std::optional<ter> err = std::nullopt;
};

struct VoteArg
{
    std::optional<Account> account = std::nullopt;
    std::uint32_t tfee = 0;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<jtx::seq> seq = std::nullopt;
    std::optional<std::pair<Issue, Issue>> assets = std::nullopt;
    std::optional<ter> err = std::nullopt;
};

struct BidArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<std::variant<int, IOUAmount, STAmount>> bidMin = std::nullopt;
    std::optional<std::variant<int, IOUAmount, STAmount>> bidMax = std::nullopt;
    std::vector<Account> authAccounts = {};
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Issue, Issue>> assets = std::nullopt;
};

/** Convenience class to test AMM functionality.
 */
class AMM
{
    Env& env_;
    Account const creatorAccount_;
    STAmount const asset1_;
    STAmount const asset2_;
    uint256 const ammID_;
    IOUAmount const initialLPTokens_;
    bool log_;
    bool doClose_;
    // Predict next purchase price
    IOUAmount lastPurchasePrice_;
    std::optional<IOUAmount> bidMin_;
    std::optional<IOUAmount> bidMax_;
    // Multi-signature
    std::optional<msig> const msig_;
    // Transaction fee
    std::uint32_t const fee_;
    AccountID const ammAccount_;
    Issue const lptIssue_;

public:
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        bool log = false,
        std::uint16_t tfee = 0,
        std::uint32_t fee = 0,
        std::optional<std::uint32_t> flags = std::nullopt,
        std::optional<jtx::seq> seq = std::nullopt,
        std::optional<jtx::msig> ms = std::nullopt,
        std::optional<ter> const& ter = std::nullopt,
        bool close = true);
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        ter const& ter,
        bool log = false,
        bool close = true);
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        CreateArg const& arg);

    /** Send amm_info RPC command
     */
    Json::Value
    ammRpcInfo(
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledgerIndex = std::nullopt,
        std::optional<Issue> issue1 = std::nullopt,
        std::optional<Issue> issue2 = std::nullopt,
        std::optional<AccountID> const& ammAccount = std::nullopt,
        bool ignoreParams = false,
        unsigned apiVersion = RPC::apiInvalidVersion) const;

    /** Verify the AMM balances.
     */
    [[nodiscard]] bool
    expectBalances(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& lpt,
        std::optional<AccountID> const& account = std::nullopt) const;

    /** Get AMM balances for the token pair.
     */
    std::tuple<STAmount, STAmount, STAmount>
    balances(
        Issue const& issue1,
        Issue const& issue2,
        std::optional<AccountID> const& account = std::nullopt) const;

    [[nodiscard]] bool
    expectLPTokens(AccountID const& account, IOUAmount const& tokens) const;

    /**
     * @param fee expected discounted fee
     * @param timeSlot expected time slot
     * @param expectedPrice expected slot price
     */
    [[nodiscard]] bool
    expectAuctionSlot(
        std::uint32_t fee,
        std::optional<std::uint8_t> timeSlot,
        IOUAmount expectedPrice) const;

    [[nodiscard]] bool
    expectAuctionSlot(std::vector<AccountID> const& authAccount) const;

    [[nodiscard]] bool
    expectTradingFee(std::uint16_t fee) const;

    [[nodiscard]] bool
    expectAmmRpcInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledger_index = std::nullopt,
        std::optional<AccountID> const& ammAccount = std::nullopt) const;

    [[nodiscard]] bool
    ammExists() const;

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        LPToken tokens,
        std::optional<STAmount> const& asset1InDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        STAmount const& asset1InDetails,
        std::optional<STAmount> const& asset2InAmount = std::nullopt,
        std::optional<STAmount> const& maxEP = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        std::optional<LPToken> tokens,
        std::optional<STAmount> const& asset1In,
        std::optional<STAmount> const& asset2In,
        std::optional<STAmount> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<std::pair<Issue, Issue>> const& assets,
        std::optional<jtx::seq> const& seq,
        std::optional<std::uint16_t> const& tfee = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    deposit(DepositArg const& arg);

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        std::optional<LPToken> const& tokens,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    withdrawAll(
        std::optional<Account> const& account,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt,
        std::optional<ter> const& ter = std::nullopt)
    {
        return withdraw(
            account,
            std::nullopt,
            asset1OutDetails,
            asset1OutDetails ? tfOneAssetWithdrawAll : tfWithdrawAll,
            ter);
    }

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        STAmount const& asset1Out,
        std::optional<STAmount> const& asset2Out = std::nullopt,
        std::optional<IOUAmount> const& maxEP = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        std::optional<LPToken> const& tokens,
        std::optional<STAmount> const& asset1Out,
        std::optional<STAmount> const& asset2Out,
        std::optional<IOUAmount> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<std::pair<Issue, Issue>> const& assets,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    withdraw(WithdrawArg const& arg);

    void
    vote(
        std::optional<Account> const& account,
        std::uint32_t feeVal,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt,
        std::optional<std::pair<Issue, Issue>> const& assets = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    vote(VoteArg const& arg);

    Json::Value
    bid(BidArg const& arg);

    AccountID const&
    ammAccount() const
    {
        return ammAccount_;
    }

    Issue
    lptIssue() const
    {
        return lptIssue_;
    }

    IOUAmount
    tokens() const
    {
        return initialLPTokens_;
    }

    IOUAmount
    getLPTokensBalance(
        std::optional<AccountID> const& account = std::nullopt) const;

    friend std::ostream&
    operator<<(std::ostream& s, AMM const& amm)
    {
        if (auto const res = amm.ammRpcInfo())
            s << res.toStyledString();
        return s;
    }

    std::string
    operator[](AccountID const& lp)
    {
        return ammRpcInfo(lp).toStyledString();
    }

    Json::Value
    operator()(AccountID const& lp)
    {
        return ammRpcInfo(lp);
    }

    void
    ammDelete(
        AccountID const& deleter,
        std::optional<ter> const& ter = std::nullopt);

    void
    setClose(bool close)
    {
        doClose_ = close;
    }

    uint256
    ammID() const
    {
        return ammID_;
    }

    void
    setTokens(
        Json::Value& jv,
        std::optional<std::pair<Issue, Issue>> const& assets = std::nullopt);

private:
    AccountID
    create(
        std::uint32_t tfee = 0,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<std::pair<Issue, Issue>> const& assets = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        Json::Value& jv,
        std::optional<jtx::seq> const& seq,
        std::optional<std::pair<Issue, Issue>> const& assets = std::nullopt,
        std::optional<ter> const& ter = std::nullopt);

    void
    log(bool log)
    {
        log_ = log;
    }

    [[nodiscard]] bool
    expectAmmInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        Json::Value const& jv) const;

    void
    submit(
        Json::Value const& jv,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter);

    [[nodiscard]] bool
    expectAuctionSlot(auto&& cb) const;
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
