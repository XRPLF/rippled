//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_TX_FIREWALLCHECK_H_INCLUDED
#define RIPPLE_APP_TX_FIREWALLCHECK_H_INCLUDED

#include <xrpld/ledger/ApplyView.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <tuple>
#include <unordered_set>

namespace ripple {

class ReadView;

class AccountRootBalance
{
    struct BalanceChange
    {
        AccountID const account;
        STAmount const balanceChange;
    };
    std::vector<BalanceChange> balanceChanges_;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ApplyView&,
        beast::Journal const&);

private:
    bool
    handleRule(
        ApplyView& view,
        SLE::pointer const& sleFirewall,
        STObject const& rule,
        STAmount const& changeAmount);

    void
    updateRuleTotal(
        ApplyView& view,
        SLE::pointer const& sleFirewall,
        STObject const& rule,
        STAmount const& totalOut);

    void
    resetRuleTimer(
        ApplyView& view,
        SLE::pointer const& sleFirewall,
        STObject const& rule,
        std::uint32_t const& currentTime);
};

class ValidWithdraw
{
    struct BalanceChange
    {
        AccountID const account;
        int const balanceChangeSign;
    };

    struct BalanceChanges
    {
        std::vector<BalanceChange> senders;
        std::vector<BalanceChange> receivers;
    };

    using ByIssuer = std::map<Issue, BalanceChanges>;
    ByIssuer balanceChanges_;

    std::map<AccountID, std::shared_ptr<SLE const> const> possibleIssuers_;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);

private:
    STAmount
    calculateBalanceChange(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after,
        bool isDelete);

    void
    recordBalance(Issue const& issue, BalanceChange change);

    void
    recordBalanceChanges(
        std::shared_ptr<SLE const> const& after,
        STAmount const& balanceChange);

    bool
    isValidEntry(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    bool
    validateWithdrawPreauth(
        ReadView const& view,
        BalanceChanges const& changes,
        STTx const& tx,
        beast::Journal const& j);
};

// additional firewall checks can be declared above and then added to this
// tuple
using FirewallChecks = std::tuple<AccountRootBalance, ValidWithdraw>;

/**
 * @brief get a tuple of all firewall checks
 *
 * @return std::tuple of instances that implement the required firewall check
 * methods
 *
 * @see ripple::FirewallChecker_PROTOTYPE
 */
inline FirewallChecks
getFirewallChecks()
{
    return FirewallChecks{};
}

}  // namespace ripple

#endif
