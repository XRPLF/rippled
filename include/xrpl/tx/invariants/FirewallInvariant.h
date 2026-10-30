#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <utility>
#include <vector>

namespace xrpl {

/**
 * A firewall keeps its owner and its counterparty.
 *
 * Every Firewall entry names a counterparty, because without one no update or
 * deletion could ever be authorized and the account's value would be stranded.
 * The owner is fixed at creation, so a firewall cannot be moved to another
 * account, and the counterparty is never the owner, which would let the owner
 * authorize its own updates.
 */
class ValidFirewall
{
    // <before, after>. before is unseated when the entry is being created.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> firewalls_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * What leaves a firewalled account is charged to its limits.
 *
 * For a transaction of the owner other than FirewallLimitSet, every matching
 * limit of every asset the owner's balances changed in carries the period's
 * Spent plus the measured charge, and a positive charge never takes a limit
 * over its LimitAmount or lets out an asset the firewall refuses. A
 * FirewallLimitSet keeps what was spent and keeps the firewall's counts equal
 * to its limits, and a failed transaction changes no limit.
 */
class ValidFirewallOutflow
{
    FirewallEntries entries_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    [[nodiscard]] bool
    checkLimitSet(ReadView const& view, AccountID const& owner, beast::Journal const& j) const;

    [[nodiscard]] bool
    checkCharges(
        STTx const& tx,
        XRPAmount fee,
        ReadView const& view,
        SLE const& firewall,
        beast::Journal const& j) const;
};

}  // namespace xrpl
