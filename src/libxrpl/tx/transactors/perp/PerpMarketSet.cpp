#include <xrpl/tx/transactors/perp/PerpMarketSet.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

bool
PerpMarketSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpMarketSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpMarketSetMask;
}

namespace {

/**
 * Whether the named oracles are 1 to kMaxPerpOracles distinct (owner, document) pairs.
 */
bool
validOracles(STArray const& oracles)
{
    if (oracles.empty() || oracles.size() > perp::kMaxPerpOracles)
        return false;

    std::set<std::pair<AccountID, std::uint32_t>> seen;
    for (auto const& oracle : oracles)
    {
        if (oracle[sfOwner] == beast::kZero ||
            !seen.emplace(oracle[sfOwner], oracle[sfOracleDocumentID]).second)
            return false;
    }
    return true;
}

/**
 * Whether the tiers are 1 to kMaxMarginTiers, ascending in notional, with
 * initial above maintenance and both rates rising from tier to tier.
 */
bool
validTiers(STArray const& tiers)
{
    if (tiers.empty() || tiers.size() > perp::kMaxMarginTiers)
        return false;

    std::optional<STObject> previous;
    for (auto const& tier : tiers)
    {
        Number const notionalMax = tier[sfNotionalMax];
        std::uint32_t const initial = tier[sfInitialMarginRate];
        std::uint32_t const maintenance = tier[sfMaintenanceMarginRate];
        if (notionalMax.signum() <= 0 || maintenance == 0 || initial <= maintenance ||
            initial > perp::kRateUnity)
            return false;

        if (previous &&
            (notionalMax <= (*previous)[sfNotionalMax] ||
             initial <= (*previous)[sfInitialMarginRate] ||
             maintenance <= (*previous)[sfMaintenanceMarginRate]))
            return false;
        previous = tier;
    }
    return true;
}

}  // namespace

NotTEC
PerpMarketSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (auto const marketID = tx[~sfMarketID])
    {
        // An update may not change the fixed parameters or the isolation flag.
        std::array<SField const*, 5> const fixed{
            &sfExchangeID, &sfBaseAsset, &sfQuoteAsset, &sfPriceTick, &sfLotSize};
        for (auto const* field : fixed)
        {
            if (tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (*marketID == beast::kZero || tx.isFlag(tfPerpIsolatedOnly))
            return temMALFORMED;
    }
    else
    {
        // Creation carries every required parameter of the market.
        std::array<SField const*, 20> const required{
            &sfExchangeID,
            &sfBaseAsset,
            &sfQuoteAsset,
            &sfIndexOracles,
            &sfIndexMaxAge,
            &sfIndexMaxLedgers,
            &sfPriceTick,
            &sfLotSize,
            &sfMarginTiers,
            &sfOpenInterestMax,
            &sfOrderPriceBand,
            &sfMidSpreadMax,
            &sfMarkBand,
            &sfMarkMoveMax,
            &sfBasisWindow,
            &sfFundingCap,
            &sfLiquidationPenalty,
            &sfLiquidationRewardShare,
            &sfLiquidationChunkMin,
            &sfSettleStaleAfter};
        for (auto const* field : required)
        {
            if (!tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (tx[sfExchangeID] == beast::kZero || tx[sfBaseAsset] == tx[sfQuoteAsset] ||
            tx.isFlag(tfPerpMarketSettle))
            return temMALFORMED;
    }

    if (tx.isFieldPresent(sfIndexOracles) && !validOracles(tx.getFieldArray(sfIndexOracles)))
        return temMALFORMED;

    if (tx.isFieldPresent(sfMarginTiers) && !validTiers(tx.getFieldArray(sfMarginTiers)))
        return temMALFORMED;

    // Increments, caps and chunk sizes are positive; the reward floor is not negative.
    for (auto const* field : {&sfPriceTick, &sfLotSize, &sfOpenInterestMax, &sfLiquidationChunkMin})
    {
        if (auto const value = tx[~*field]; value && value->signum() <= 0)
            return temMALFORMED;
    }
    if (auto const rewardMin = tx[~sfLiquidationRewardMin]; rewardMin && rewardMin->signum() < 0)
        return temMALFORMED;

    for (auto const* field :
         {&sfOrderPriceBand,
          &sfMidSpreadMax,
          &sfMarkBand,
          &sfMarkMoveMax,
          &sfLiquidationPenalty,
          &sfLiquidationRewardShare})
    {
        if (!validNumericRange(tx[~*field], perp::kRateUnity))
            return temMALFORMED;
    }

    if (!validNumericRange(tx[~sfFundingCap], perp::kMaxFundingCap))
        return temMALFORMED;

    if (tx.isFieldPresent(sfMidSpreadMax) && tx.isFieldPresent(sfOrderPriceBand) &&
        tx[sfMidSpreadMax] >= tx[sfOrderPriceBand])
        return temMALFORMED;

    if (tx[~sfIndexMaxAge] == 0u || tx[~sfIndexMaxLedgers] == 0u || tx[~sfBasisWindow] == 0u)
        return temMALFORMED;

    if (auto const staleAfter = tx[~sfSettleStaleAfter];
        staleAfter && *staleAfter < perp::kMinSettleStaleAfter.count())
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpMarketSet::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpMarketSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpMarketSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
