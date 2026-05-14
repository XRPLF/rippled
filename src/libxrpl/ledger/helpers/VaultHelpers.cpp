/** @file
 *  Implements asset/share conversion arithmetic for the XLS-65d Single Asset
 *  Vault feature.
 *
 *  The four functions here are the sole location where the deposit and
 *  withdrawal exchange rates are computed. Key design invariants:
 *
 *  - **Deposit functions** use raw `sfAssetsTotal` as the pool size.
 *    Unrealized losses are not visible to incoming depositors — they are a
 *    risk socialised across existing shareholders, not a discount for new ones.
 *
 *  - **Withdrawal functions** subtract `sfLossUnrealized` from `sfAssetsTotal`
 *    before computing the rate, so departing shareholders bear their pro-rata
 *    share of any recorded losses.
 *
 *  - Every result is a whole number because vault shares are integer MPTs.
 *    Deposit functions always truncate (vault-favorable); the withdrawal
 *    direction supports optional truncation via `TruncateShares`.
 *
 *  - All functions use a double-check guard: `XRPL_ASSERT` fires in debug
 *    builds on bad inputs; an identical `if` returns `std::nullopt` in
 *    production. The `nullopt` branches are marked `LCOV_EXCL_LINE` because
 *    callers have already validated preconditions in `preclaim`.
 */
#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep

#include <memory>
#include <optional>

namespace xrpl {

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
    {
        // Bootstrap: no assets in the vault yet, so establish the initial
        // exchange rate as 1 asset = 10^sfScale shares. Scaling up share
        // count reduces the first-depositor donation attack surface.
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    // Truncate so the depositor never receives more shares than strictly
    // warranted; the rounding residue stays in the vault.
    shares = ((shareTotal * assets) / assetTotal).truncate();
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
    {
        // Bootstrap inverse: reverse the exponent shift applied by
        // assetsToSharesDeposit() to recover the original asset amount.
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    // Withdrawal path: deduct unrealized losses before computing the rate so
    // departing holders bear their proportional share of any recorded loss.
    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return shares;  // Fully insolvent vault; zero shares required.
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / assetTotal;
    if (truncate == TruncateShares::Yes)
        result = result.truncate();
    shares = result;
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    // Withdrawal path: deduct unrealized losses so redeemers receive the
    // net asset value rather than the gross book value.
    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;  // Fully insolvent vault; zero assets delivered.
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

}  // namespace xrpl
