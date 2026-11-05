#include <xrpl/tx/invariants/FirewallInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>

namespace xrpl {

void
ValidFirewall::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    if (after && after->getType() == ltFIREWALL)
        firewalls_.emplace_back(before, after);
}

bool
ValidFirewall::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    // A firewall cannot exist unless the amendment is enabled, so the amendment
    // needs no separate check here.
    return std::ranges::all_of(firewalls_, [&j](auto const& entry) {
        auto const& [before, after] = entry;
        if (!after->isFieldPresent(sfOwner) || !after->isFieldPresent(sfCounterparty))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall is missing its owner "
                               "or its counterparty";
            return false;
        }

        if (after->getAccountID(sfOwner) == after->getAccountID(sfCounterparty))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall's counterparty is its owner";
            return false;
        }

        if (before && before->getAccountID(sfOwner) != after->getAccountID(sfOwner))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall's owner changed";
            return false;
        }

        return true;
    });
}

void
ValidFirewallOutflow::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    if (isFirewallEntry((before ? before : after)->getType()))
        entries_.emplace_back(before, isDelete ? nullptr : after);
}

bool
ValidFirewallOutflow::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!view.rules().enabled(featureFirewallLimits) || !tx.isFieldPresent(sfAccount))
        return true;

    AccountID const owner = tx.getAccountID(sfAccount);
    auto const sleFirewall = view.read(keylet::firewall(owner));
    if (!sleFirewall)
        return true;

    if (!isTesSuccess(result))
    {
        if (std::ranges::any_of(entries_, [](auto const& entry) {
                auto const& [before, after] = entry;
                return (before ? before : after)->getType() == ltFIREWALL_LIMIT;
            }))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall limit changed on a failed "
                               "transaction";
            return false;
        }
        return true;
    }

    if (tx.getTxnType() == ttFIREWALL_LIMIT_SET)
        return checkLimitSet(view, owner, j);

    return checkCharges(tx, fee, view, *sleFirewall, j);
}

bool
ValidFirewallOutflow::checkLimitSet(
    ReadView const& view,
    AccountID const& owner,
    beast::Journal const& j) const
{
    auto const now = view.parentCloseTime();
    std::int64_t limitDelta = 0;
    std::int64_t signerDelta = 0;
    std::int64_t countDelta = 0;
    std::int64_t signerCountDelta = 0;

    for (auto const& [before, after] : entries_)
    {
        SLE const& sle = before ? *before : *after;
        if (sle.getType() == ltFIREWALL)
        {
            auto const count = [](SLE::const_pointer const& firewall, SF_UINT32 const& field) {
                return firewall ? std::int64_t{(*firewall)[~field].value_or(0)} : 0;
            };
            if (sle.getAccountID(sfOwner) == owner)
            {
                countDelta = count(after, sfLimitCount) - count(before, sfLimitCount);
                signerCountDelta =
                    count(after, sfSignerLimitCount) - count(before, sfSignerLimitCount);
            }
            continue;
        }

        if (sle.getType() != ltFIREWALL_LIMIT)
            continue;

        if (!before)
        {
            ++limitDelta;
            if (after->isFieldPresent(sfLimitSigner))
                ++signerDelta;
            if (after->getFieldAmount(sfSpent) != beast::kZero)
            {
                JLOG(j.fatal()) << "Invariant failed: a created firewall limit has spent";
                return false;
            }
        }
        else if (!after)
        {
            --limitDelta;
            if (before->isFieldPresent(sfLimitSigner))
                --signerDelta;
        }
        else
        {
            auto const period = firewallLimitPeriod(*before, now);
            if (after->getFieldAmount(sfSpent) != period.spent ||
                after->getFieldU32(sfStartDate) != period.startDate)
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit update changed what "
                                   "was spent";
                return false;
            }
        }
    }

    if (limitDelta != countDelta || signerDelta != signerCountDelta)
    {
        JLOG(j.fatal()) << "Invariant failed: a firewall's limit counts do not match its limits";
        return false;
    }

    return true;
}

bool
ValidFirewallOutflow::checkCharges(
    STTx const& tx,
    XRPAmount fee,
    ReadView const& view,
    SLE const& firewall,
    beast::Journal const& j) const
{
    AccountID const owner = tx.getAccountID(sfAccount);
    AccountID const backup = firewall.getAccountID(sfBackup);
    bool const refusesUnlisted = firewall[~sfUnlistedAssets].value_or(0) == 1;
    bool const unmatchedSigner =
        tx.isFlag(tfInnerBatchTxn) && firewall[~sfSignerLimitCount].value_or(0) > 0;
    auto const now = view.parentCloseTime();

    for (auto const& asset : firewallChargedAssets(entries_, tx, owner))
    {
        Number const charge = firewallCharge(entries_, tx, owner, backup, asset, fee);
        if (unmatchedSigner && charge > Number{})
        {
            JLOG(j.fatal()) << "Invariant failed: a Batch inner transaction moved an asset out "
                               "of an account with a signer's firewall limit";
            return false;
        }
        bool matched = false;
        for (auto const& limitKeylet : firewallMatchingLimits(tx, owner, asset))
        {
            auto const modified = std::ranges::find_if(entries_, [&](auto const& entry) {
                auto const& [before, after] = entry;
                return (before ? before : after)->key() == limitKeylet.key;
            });
            SLE::const_pointer const before =
                modified == entries_.end() ? view.read(limitKeylet) : modified->first;
            SLE::const_pointer const after = modified == entries_.end() ? before : modified->second;
            if (!before)
                continue;
            matched = true;

            if (!after)
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit was deleted by a charge";
                return false;
            }

            auto const period = firewallLimitPeriod(*before, now);
            Number const spent = Number(period.spent) + charge;
            if (charge > Number{} && spent > Number(before->getFieldAmount(sfLimitAmount)))
            {
                JLOG(j.fatal()) << "Invariant failed: an outflow exceeded a firewall limit";
                return false;
            }

            if (after->getFieldAmount(sfSpent) != STAmount{asset, spent})
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit's spent does not "
                                   "match its charge";
                return false;
            }
        }

        if (!matched && refusesUnlisted && charge > Number{})
        {
            JLOG(j.fatal()) << "Invariant failed: an asset with no firewall limit left an "
                               "account that refuses unlisted assets";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
