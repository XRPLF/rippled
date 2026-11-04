#include <xrpl/tx/transactors/perp/PerpExchangeDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

bool
PerpExchangeDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpExchangeDelete::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpExchangeDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (ctx.tx[sfExchangeID] == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpExchangeDelete::preclaim(PreclaimContext const& ctx)
{
    auto const exchange = ctx.view.read(keylet::perpExchange(ctx.tx[sfExchangeID]));
    if (!exchange)
        return tecNO_ENTRY;
    if ((*exchange)[sfOwner] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // Markets, margin accounts and bad debt not yet recovered all hold claims on the collateral.
    if ((*exchange)[sfMarketCount] != 0 || (*exchange)[sfMarginAccountCount] != 0 ||
        (*exchange)[sfBadDebt] != Number{})
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
PerpExchangeDelete::doApply()
{
    auto const exchange = view().peek(keylet::perpExchange(ctx_.tx[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const owner = view().peek(keylet::account(accountID_));
    if (!owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    AccountID const pseudoID = (*exchange)[sfAccount];
    auto const asset = (*exchange)[sfAsset];

    if (!view().dirRemove(
            keylet::ownerDir(accountID_), (*exchange)[sfOwnerNode], exchange->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Released first, so a holding created for the payout below can use the freed reserve.
    decreaseOwnerCountForObject(view(), owner, exchange, 2, j_);

    // Every asset the pseudo-account holds goes back to the owner.
    STAmount const assets{asset, (*exchange)[sfAssetsTotal]};
    if (assets > beast::kZero)
    {
        if (!holdingExists(view(), accountID_, asset))
        {
            if (auto const ter = addEmptyHolding(
                    ctx_.getApplyViewContext(), accountID_, preFeeBalance_, asset, j_))
                return ter;
        }
        if (auto const ter =
                accountSend(view(), pseudoID, accountID_, assets, j_, {}, WaiveTransferFee::Yes))
            return ter;
    }

    if (auto const ter = removeEmptyHolding(ctx_.getApplyViewContext(), pseudoID, asset, j_))
        return ter;

    auto const pseudo = view().peek(keylet::account(pseudoID));
    if (!pseudo)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    if (STAmount{(*pseudo)[sfBalance]} != beast::kZero || (*pseudo)[sfOwnerCount] != 0 ||
        view().exists(keylet::ownerDir(pseudoID)))
    {
        JLOG(j_.warn()) << "PerpExchangeDelete: pseudo-account still holds assets or objects";
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE
    }
    view().erase(pseudo);

    // The backstop vault no longer backs an exchange.
    if (auto const vaultID = (*exchange)[~sfVaultID])
    {
        if (auto const vault = view().peek(keylet::vault(*vaultID)))
        {
            vault->makeFieldAbsent(sfPerpExchangeID);
            view().update(vault);
        }
    }

    view().erase(exchange);
    return tesSUCCESS;
}

void
PerpExchangeDelete::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpExchangeDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
