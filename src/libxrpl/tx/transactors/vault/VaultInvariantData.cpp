#include <xrpl/tx/transactors/vault/VaultInvariantData.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace xrpl {

static constexpr Number kZero{};

VaultInvariantData::Vault
VaultInvariantData::Vault::make(SLE const& from)
{
    XRPL_ASSERT(from.getType() == ltVAULT, "VaultInvariantData::Vault::make : from Vault object");

    VaultInvariantData::Vault self;
    self.key = from.key();
    self.asset = from.at(sfAsset);
    self.pseudoId = from.getAccountID(sfAccount);
    self.owner = from.at(sfOwner);
    self.shareMPTID = from.getFieldH192(sfShareMPTID);
    self.assetsTotal = from.at(sfAssetsTotal);
    self.assetsAvailable = from.at(sfAssetsAvailable);
    self.assetsMaximum = from.at(sfAssetsMaximum);
    self.lossUnrealized = from.at(sfLossUnrealized);
    return self;
}

VaultInvariantData::Shares
VaultInvariantData::Shares::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltMPTOKEN_ISSUANCE,
        "VaultInvariantData::Shares::make : from MPTokenIssuance object");

    VaultInvariantData::Shares self;
    self.sleKey = from.key();
    self.share = MPTIssue(makeMptID(from.getFieldU32(sfSequence), from.getAccountID(sfIssuer)));
    self.sharesTotal = from.at(sfOutstandingAmount);
    self.sharesMaximum = from[~sfMaximumAmount].value_or(kMaxMpTokenAmount);
    return self;
}

void
VaultInvariantData::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // If `before` is empty, this means an object is being created, in which
    // case `isDelete` must be false. Otherwise `before` and `after` are set and
    // `isDelete` indicates whether an object is being deleted or modified.
    XRPL_ASSERT(
        after != nullptr && (before != nullptr || !isDelete),
        "xrpl::VaultInvariantData::visitEntry : some object is available");

    // Number balanceDelta will capture the difference (delta) between "before"
    // state (zero if created) and "after" state (zero if destroyed), and
    // preserves value scale (exponent) to round values to the same scale during
    // validation. It is used to validate that the change in account
    // balances matches the change in vault balances, stored to deltas_ at the
    // end of this function.
    DeltaInfo balanceDelta{.delta = kNumZero, .scale = std::nullopt};

    std::int8_t sign = 0;
    if (before)
    {
        switch (before->getType())
        {
            case ltVAULT:
                beforeVault_.push_back(Vault::make(*before));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                beforeMPTs_.push_back(Shares::make(*before));
                balanceDelta.delta =
                    static_cast<std::int64_t>(before->getFieldU64(sfOutstandingAmount));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta.delta = static_cast<std::int64_t>(before->getFieldU64(sfMPTAmount));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
                balanceDelta.delta = before->getFieldAmount(sfBalance);
                // Account balance is XRP, which is an int, so the scale is
                // always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltRIPPLE_STATE: {
                auto const amount = before->getFieldAmount(sfBalance);
                balanceDelta.delta = amount;
                // Trust Line balances are STAmounts, so we can use the exponent
                // directly to get the scale.
                balanceDelta.scale = amount.exponent();
                sign = -1;
                break;
            }
            default:;
        }
    }

    if (!isDelete && after)
    {
        switch (after->getType())
        {
            case ltVAULT:
                afterVault_.push_back(Vault::make(*after));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                afterMPTs_.push_back(Shares::make(*after));
                balanceDelta.delta -=
                    Number(static_cast<std::int64_t>(after->getFieldU64(sfOutstandingAmount)));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta.delta -=
                    Number(static_cast<std::int64_t>(after->getFieldU64(sfMPTAmount)));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
                balanceDelta.delta -= Number(after->getFieldAmount(sfBalance));
                // Account balance is XRP, which is an int, so the scale is
                // always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltRIPPLE_STATE: {
                auto const amount = after->getFieldAmount(sfBalance);
                balanceDelta.delta -= Number(amount);
                // Trust Line balances are STAmounts, so we can use the exponent
                // directly to get the scale.
                if (amount.exponent() > balanceDelta.scale)
                    balanceDelta.scale = amount.exponent();
                sign = -1;
                break;
            }
            default:;
        }
    }

    uint256 const key = (before ? before->key() : after->key());
    // Append to deltas if sign is non-zero, i.e. an object of an interesting
    // type has been updated. A transaction may update an object even when
    // its balance has not changed, e.g. transaction fee equals the amount
    // transferred to the account. We intentionally do not compare balanceDelta
    // against zero, to avoid missing such updates.
    if (sign != 0)
    {
        XRPL_ASSERT_PARTS(
            balanceDelta.scale, "xrpl::VaultInvariantData::visitEntry", "scale initialized");
        balanceDelta.delta *= sign;
        deltas_[key] = balanceDelta;
    }
}

std::optional<VaultInvariantData::Shares>
VaultInvariantData::resolveUpdatedShares(Vault const& afterVault) const
{
    auto const targetKey = keylet::mptokenIssuance(afterVault.shareMPTID).key;
    for (auto const& e : afterMPTs_)
    {
        if (e.sleKey == targetKey)
            return e;
    }
    return std::nullopt;
}

std::optional<VaultInvariantData::Shares>
VaultInvariantData::resolveBeforeShares(Vault const& beforeVault) const
{
    auto const targetKey = keylet::mptokenIssuance(beforeVault.shareMPTID).key;
    for (auto const& e : beforeMPTs_)
    {
        if (e.sleKey == targetKey)
            return e;
    }
    return std::nullopt;
}

std::optional<VaultInvariantData::DeltaInfo>
VaultInvariantData::deltaAssets(AccountID const& id) const
{
    auto const& vaultAsset = afterVault_[0].asset;
    auto const lookup = [&](uint256 const& key) -> std::optional<DeltaInfo> {
        auto const it = deltas_.find(key);
        if (it == deltas_.end())
            return std::nullopt;
        return it->second;
    };

    return std::visit(
        [&]<typename TIss>(TIss const& issue) -> std::optional<DeltaInfo> {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                if (isXRP(issue))
                    return lookup(keylet::account(id).key);
                auto result = lookup(keylet::trustLine(id, issue).key);
                // Trust-line balance is stored from the low-account's perspective;
                // negate if id is the high account so the delta is in id's terms.
                if (result && id > issue.getIssuer())
                    result->delta = -result->delta;
                return result;
            }
            else if constexpr (std::is_same_v<TIss, MPTIssue>)
            {
                return lookup(keylet::mptoken(issue.getMptID(), id).key);
            }
        },
        vaultAsset.value());
}

std::optional<VaultInvariantData::DeltaInfo>
VaultInvariantData::deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const
{
    auto const& vaultAsset = afterVault_[0].asset;
    auto ret = deltaAssets(tx[sfAccount]);
    if (!ret.has_value() || !vaultAsset.native())
        return ret;

    // Only add the fee back if tx[sfAccount] actually paid it. When the fee is
    // paid by someone else (a delegate or a fee sponsor), the
    // account's XRP balance moved only by the vault amount.
    if (tx.getFeePayerID() != tx[sfAccount])
        return ret;

    ret->delta += fee.drops();
    if (ret->delta == kZero)
        return std::nullopt;

    return ret;
}

std::optional<VaultInvariantData::DeltaInfo>
VaultInvariantData::deltaShares(AccountID const& id) const
{
    auto const& afterVault = afterVault_[0];
    auto const it = [&]() {
        if (id == afterVault.pseudoId)
            return deltas_.find(keylet::mptokenIssuance(afterVault.shareMPTID).key);
        return deltas_.find(keylet::mptoken(afterVault.shareMPTID, id).key);
    }();

    return it != deltas_.end() ? std::optional<DeltaInfo>(it->second) : std::nullopt;
}

bool
VaultInvariantData::isVaultEmpty(Vault const& vault)
{
    return vault.assetsAvailable == 0 && vault.assetsTotal == 0;
}

std::int32_t
VaultInvariantData::computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const
{
    // Returns the posterior `assetsTotal` scale.
    //
    // 1. Because STAmounts are normalized, `assetsTotal` (being >= `assetsAvailable`)
    // safely represents the coarsest exponent needed for both fields.
    //
    // 2. The scale may decrease (withdraw/clawback) or increase (deposit). In both cases
    // we ensure the vault is in a legitimate state in the post-transaction scale.
    auto const& afterVault = afterVault_[0];
    auto const& vaultAsset = afterVault.asset;
    if (rules.enabled(fixCleanup3_2_0))
    {
        NumberRoundModeGuard const roundGuard(Number::RoundingMode::ToNearest);
        return scale(afterVault.assetsTotal, vaultAsset);
    }

    auto const& beforeVault = beforeVault_[0];
    auto const totalDelta =
        DeltaInfo::makeDelta(beforeVault.assetsTotal, afterVault.assetsTotal, vaultAsset);
    auto const availableDelta =
        DeltaInfo::makeDelta(beforeVault.assetsAvailable, afterVault.assetsAvailable, vaultAsset);
    return computeCoarsestScale({vaultDelta, totalDelta, availableDelta});
}

[[nodiscard]] VaultInvariantData::DeltaInfo
VaultInvariantData::DeltaInfo::makeDelta(
    Number const& before,
    Number const& after,
    Asset const& asset)
{
    return {
        .delta = after - before,
        .scale = std::max(xrpl::scale(after, asset), xrpl::scale(before, asset))};
}

[[nodiscard]] std::int32_t
VaultInvariantData::computeCoarsestScale(std::vector<DeltaInfo> const& numbers)
{
    if (numbers.empty())
        return 0;

    auto const max = std::ranges::max_element(
        numbers, [](auto const& a, auto const& b) -> bool { return a.scale < b.scale; });
    XRPL_ASSERT_PARTS(
        max->scale,
        "xrpl::VaultInvariantData::computeCoarsestScale",
        "scale set for destinationDelta");
    return max->scale.value_or(STAmount::kMaxOffset);
}

}  // namespace xrpl
