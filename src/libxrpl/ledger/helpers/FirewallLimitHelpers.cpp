#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace xrpl {

namespace {

// What `account` holds of `asset` on one entry, or unseated when the entry
// holds none of it for the account. On a trust line the account holds the
// peer's IOU on the positive side of its balance and owes its own on the
// negative side.
std::optional<Number>
heldOn(SLE const& sle, AccountID const& account, Asset const& asset)
{
    switch (sle.getType())
    {
        case ltACCOUNT_ROOT:
            if (asset.native() && sle.getAccountID(sfAccount) == account)
                return Number(sle.getFieldAmount(sfBalance));
            return std::nullopt;

        case ltRIPPLE_STATE: {
            if (!asset.holds<Issue>() || asset.native())
                return std::nullopt;
            auto const& issue = asset.get<Issue>();
            STAmount const balance = sle.getFieldAmount(sfBalance);
            if (balance.get<Issue>().currency != issue.currency)
                return std::nullopt;
            AccountID const low = sle.getFieldAmount(sfLowLimit).getIssuer();
            AccountID const high = sle.getFieldAmount(sfHighLimit).getIssuer();
            if (account != low && account != high)
                return std::nullopt;
            AccountID const peer = account == low ? high : low;
            Number const own = account == low ? Number(balance) : -Number(balance);
            if (issue.account == peer)
                return std::max(own, Number{});
            if (issue.account == account)
                return std::min(own, Number{});
            return std::nullopt;
        }

        case ltMPTOKEN:
            if (asset.holds<MPTIssue>() && sle.getAccountID(sfAccount) == account &&
                sle.getFieldH192(sfMPTokenIssuanceID) == asset.get<MPTIssue>().getMptID())
            {
                return Number(static_cast<std::int64_t>(sle[~sfMPTAmount].value_or(0)));
            }
            return std::nullopt;

        default:
            return std::nullopt;
    }
}

Number
heldOn(SLE::const_pointer const& sle, AccountID const& account, Asset const& asset)
{
    if (!sle)
        return Number{};
    return heldOn(*sle, account, asset).value_or(Number{});
}

// The account's net increase of the asset over the entry pairs.
Number
increaseOf(FirewallEntries const& entries, AccountID const& account, Asset const& asset)
{
    Number increase;
    for (auto const& [before, after] : entries)
        increase += heldOn(after, account, asset) - heldOn(before, account, asset);
    return increase;
}

void
addAsset(std::vector<Asset>& assets, Asset const& asset)
{
    if (std::ranges::find(assets, asset) == assets.end())
        assets.push_back(asset);
}

}  // namespace

FirewallLimitPeriod
firewallLimitPeriod(STObject const& limit, NetClock::time_point now)
{
    STAmount const spent = limit.getFieldAmount(sfSpent);
    std::uint64_t const start = limit.getFieldU32(sfStartDate);
    std::uint64_t const period = limit.getFieldU32(sfPeriod);
    std::uint64_t const at = now.time_since_epoch().count();

    if (period == 0 || at < start + period)
        return {.spent = spent, .startDate = static_cast<std::uint32_t>(start)};

    // A new period starts a whole number of periods after the stored one.
    return {
        .spent = STAmount{spent.asset()},
        .startDate = static_cast<std::uint32_t>(start + (period * ((at - start) / period))),
    };
}

std::optional<FirewallLimitField>
firewallLimitField(AccountID const& owner, Asset const& asset)
{
    if (asset.native())
        return FirewallLimitField{.entry = keylet::account(owner), .field = &sfFirewallLimit};

    return asset.visit(
        [&owner](Issue const& issue) -> std::optional<FirewallLimitField> {
            if (issue.account == owner)
                return std::nullopt;
            return FirewallLimitField{
                .entry = keylet::trustLine(owner, issue.account, issue.currency),
                .field = owner < issue.account ? &sfLowFirewallLimit : &sfHighFirewallLimit,
            };
        },
        [&owner](MPTIssue const& issue) -> std::optional<FirewallLimitField> {
            if (issue.getIssuer() == owner)
                return std::nullopt;
            return FirewallLimitField{
                .entry = keylet::mptoken(issue.getMptID(), owner),
                .field = &sfFirewallLimit,
            };
        });
}

bool
isFirewallEntry(LedgerEntryType type) noexcept
{
    return type == ltACCOUNT_ROOT || type == ltRIPPLE_STATE || type == ltMPTOKEN ||
        type == ltFIREWALL;
}

std::optional<STAmount>
firewallFieldBound(STTx const& tx, AccountID const& backup)
{
    std::optional<STAmount> bound;
    switch (tx.getTxnType())
    {
        case ttPAYMENT:
            // Value swept to the backup is not charged.
            if (tx.getAccountID(sfDestination) != backup)
                bound = tx[~sfSendMax].value_or(tx[sfAmount]);
            break;
        case ttESCROW_CREATE:
        case ttPAYCHAN_CREATE:
        case ttPAYCHAN_FUND:
            bound = tx[sfAmount];
            break;
        case ttCHECK_CREATE:
        case ttNFTOKEN_CREATE_OFFER:
            bound = firewallCommitment(tx);
            break;
        default:
            break;
    }

    if (bound && bound->signum() <= 0)
        return std::nullopt;
    return bound;
}

std::optional<STAmount>
firewallCommitment(STTx const& tx)
{
    std::optional<STAmount> commitment;
    if (tx.getTxnType() == ttCHECK_CREATE)
    {
        commitment = tx[sfSendMax];
    }
    else if (tx.getTxnType() == ttNFTOKEN_CREATE_OFFER && !tx.isFlag(tfSellNFToken))
    {
        commitment = tx[sfAmount];
    }

    if (commitment && commitment->signum() <= 0)
        return std::nullopt;
    return commitment;
}

std::vector<Asset>
firewallChargedAssets(FirewallEntries const& entries, STTx const& tx, AccountID const& owner)
{
    std::vector<Asset> assets;
    auto const addIfChanged =
        [&](SLE::const_pointer const& before, SLE::const_pointer const& after, Asset const& asset) {
            if (heldOn(before, owner, asset) != heldOn(after, owner, asset))
                addAsset(assets, asset);
        };

    for (auto const& [before, after] : entries)
    {
        SLE const& sle = before ? *before : *after;
        switch (sle.getType())
        {
            case ltACCOUNT_ROOT:
                addIfChanged(before, after, xrpIssue());
                break;
            case ltRIPPLE_STATE: {
                AccountID const low = sle.getFieldAmount(sfLowLimit).getIssuer();
                AccountID const high = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (owner != low && owner != high)
                    break;
                Currency const& currency = sle.getFieldAmount(sfBalance).get<Issue>().currency;
                addIfChanged(before, after, Issue{currency, owner == low ? high : low});
                addIfChanged(before, after, Issue{currency, owner});
                break;
            }
            case ltMPTOKEN:
                addIfChanged(before, after, MPTIssue{sle.getFieldH192(sfMPTokenIssuanceID)});
                break;
            default:
                break;
        }
    }

    if (auto const commitment = firewallCommitment(tx))
        addAsset(assets, commitment->asset());

    return assets;
}

Number
firewallOutflow(
    FirewallEntries const& entries,
    STTx const& tx,
    AccountID const& owner,
    Asset const& asset,
    XRPAmount fee)
{
    Number outflow = -increaseOf(entries, owner, asset);
    // The fee is bounded by the firewall's MaxFee, not by a limit.
    if (asset.native() && tx.getFeePayerID() == owner)
        outflow -= Number(STAmount{fee});
    return outflow;
}

Number
firewallCharge(
    FirewallEntries const& entries,
    STTx const& tx,
    AccountID const& owner,
    AccountID const& backup,
    Asset const& asset,
    XRPAmount fee)
{
    Number charge = firewallOutflow(entries, tx, owner, asset, fee);
    if (auto const commitment = firewallCommitment(tx); commitment && commitment->asset() == asset)
        charge += Number(*commitment);
    charge -= increaseOf(entries, backup, asset);
    return std::max(charge, Number{});
}

}  // namespace xrpl
