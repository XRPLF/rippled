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
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

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

namespace {

// The limit fields an entry of this type can carry.
std::vector<SField const*>
limitFieldsOf(LedgerEntryType type)
{
    switch (type)
    {
        case ltACCOUNT_ROOT:
        case ltMPTOKEN:
            return {&sfFirewallLimit};
        case ltRIPPLE_STATE:
            return {&sfLowFirewallLimit, &sfHighFirewallLimit};
        default:
            return {};
    }
}

// The account whose limit the field of the entry is: the AccountRoot's or
// MPToken's account, or the issuer of the line side's trust limit.
AccountID
limitOwner(SLE const& sle, SField const& field)
{
    if (sle.getType() != ltRIPPLE_STATE)
        return sle.getAccountID(sfAccount);
    return sle.getFieldAmount(&field == &sfLowFirewallLimit ? sfLowLimit : sfHighLimit).getIssuer();
}

// The limit stored in the field of the entry, unseated when the entry is
// absent or does not carry it.
std::optional<STObject>
limitOn(SLE::const_pointer const& sle, SField const& field)
{
    if (!sle || !sle->isFieldPresent(field))
        return std::nullopt;
    return sle->getFieldObject(field);
}

}  // namespace

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
    if (!view.rules().enabled(featureFirewallLimits))
        return true;

    if (!checkLimitFields(tx, result, j))
        return false;

    if (!isTesSuccess(result) || !tx.isFieldPresent(sfAccount))
        return true;

    AccountID const owner = tx.getAccountID(sfAccount);
    auto const sleFirewall = view.read(keylet::firewall(owner));
    if (!sleFirewall)
        return true;

    if (tx.getTxnType() == ttFIREWALL_LIMIT_SET)
        return checkLimitSet(view, owner, j);

    return checkCharges(tx, fee, view, *sleFirewall, j);
}

bool
ValidFirewallOutflow::checkLimitFields(STTx const& tx, TER const result, beast::Journal const& j)
    const
{
    bool const isLimitSet = tx.getTxnType() == ttFIREWALL_LIMIT_SET;
    bool const isLimitDelete = isLimitSet && tx.isFlag(tfLimitDelete);
    std::optional<AccountID> const sender = isTesSuccess(result) && tx.isFieldPresent(sfAccount)
        ? std::optional<AccountID>{tx.getAccountID(sfAccount)}
        : std::nullopt;

    for (auto const& [before, after] : entries_)
    {
        SLE const& sle = before ? *before : *after;
        for (SField const* field : limitFieldsOf(sle.getType()))
        {
            auto const limitBefore = limitOn(before, *field);
            auto const limitAfter = limitOn(after, *field);
            if (limitBefore == limitAfter)
                continue;

            bool const byOwner = sender == limitOwner(sle, *field);
            if (!limitBefore && (!byOwner || !isLimitSet || isLimitDelete))
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit was added other than by "
                                   "its owner's FirewallLimitSet";
                return false;
            }
            if (!limitAfter && (!byOwner || !isLimitDelete))
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit was removed other than by "
                                   "its owner's FirewallLimitSet";
                return false;
            }
            if (!byOwner)
            {
                JLOG(j.fatal()) << "Invariant failed: a firewall limit changed other than by a "
                                   "successful transaction of its owner";
                return false;
            }
        }
    }

    return true;
}

bool
ValidFirewallOutflow::checkLimitSet(
    ReadView const& view,
    AccountID const& owner,
    beast::Journal const& j) const
{
    auto const now = view.parentCloseTime();
    std::int64_t limitDelta = 0;
    std::int64_t countDelta = 0;

    for (auto const& [before, after] : entries_)
    {
        SLE const& sle = before ? *before : *after;
        if (sle.getType() == ltFIREWALL)
        {
            auto const count = [](SLE::const_pointer const& firewall) {
                return firewall ? std::int64_t{(*firewall)[~sfLimitCount].value_or(0)} : 0;
            };
            if (sle.getAccountID(sfOwner) == owner)
                countDelta = count(after) - count(before);
            continue;
        }

        for (SField const* field : limitFieldsOf(sle.getType()))
        {
            auto const limitBefore = limitOn(before, *field);
            auto const limitAfter = limitOn(after, *field);
            if (!limitBefore && limitAfter)
            {
                ++limitDelta;
                if (limitAfter->getFieldAmount(sfSpent) != beast::kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: a created firewall limit has spent";
                    return false;
                }
            }
            else if (limitBefore && !limitAfter)
            {
                --limitDelta;
            }
            else if (limitBefore && limitAfter && *limitBefore != *limitAfter)
            {
                auto const period = firewallLimitPeriod(*limitBefore, now);
                if (limitAfter->getFieldAmount(sfSpent) != period.spent ||
                    limitAfter->getFieldU32(sfStartDate) != period.startDate)
                {
                    JLOG(j.fatal()) << "Invariant failed: a firewall limit update changed what "
                                       "was spent";
                    return false;
                }
            }
        }
    }

    if (limitDelta != countDelta)
    {
        JLOG(j.fatal()) << "Invariant failed: a firewall's limit count does not match its limits";
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
    auto const now = view.parentCloseTime();

    for (auto const& asset : firewallChargedAssets(entries_, tx, owner))
    {
        Number const charge = firewallCharge(entries_, tx, owner, backup, asset, fee);
        auto const location = firewallLimitField(owner, asset);
        std::optional<STObject> limitBefore;
        std::optional<STObject> limitAfter;
        if (location)
        {
            auto const modified = std::ranges::find_if(entries_, [&](auto const& entry) {
                auto const& [before, after] = entry;
                return (before ? before : after)->key() == location->entry.key;
            });
            SLE::const_pointer const before =
                modified == entries_.end() ? view.read(location->entry) : modified->first;
            SLE::const_pointer const after = modified == entries_.end() ? before : modified->second;
            limitBefore = limitOn(before, *location->field);
            limitAfter = limitOn(after, *location->field);
        }

        if (!limitBefore)
        {
            if (refusesUnlisted && charge > Number{})
            {
                JLOG(j.fatal()) << "Invariant failed: an asset with no firewall limit left an "
                                   "account that refuses unlisted assets";
                return false;
            }
            continue;
        }

        auto const period = firewallLimitPeriod(*limitBefore, now);
        Number const spent = Number(period.spent) + charge;
        if (charge > Number{} && spent > Number(limitBefore->getFieldAmount(sfLimitAmount)))
        {
            JLOG(j.fatal()) << "Invariant failed: an outflow exceeded a firewall limit";
            return false;
        }

        if (!limitAfter || limitAfter->getFieldAmount(sfSpent) != STAmount{asset, spent})
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall limit's spent does not "
                               "match its charge";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
