#include <xrpl/tx/transactors/perp/PerpExchangeSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>
#include <memory>

namespace xrpl {

bool
PerpExchangeSet::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return false;

    return !ctx.tx.isFieldPresent(sfDomainID) || ctx.rules.enabled(featurePermissionedDomains);
}

std::uint32_t
PerpExchangeSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpExchangeSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (!validDataLength(tx[~sfData], kMaxDataPayloadLength))
        return temMALFORMED;

    if (!validNumericRange(tx[~sfMakerFee], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfTakerFee], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfBuilderFeeMax], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfOwnerFeeShare], perp::kRateUnity) ||
        !validNumericRange(tx[~sfVaultFeeShare], perp::kRateUnity) ||
        !validNumericRange(tx[~sfCoverRateMinimum], perp::kRateUnity) ||
        !validNumericRange(tx[~sfVaultLossLimit], perp::kRateUnity))
        return temMALFORMED;

    if (tx[~sfOwnerFeeShare].value_or(0) + tx[~sfVaultFeeShare].value_or(0) > perp::kRateUnity)
        return temMALFORMED;

    if (auto const exchangeID = tx[~sfExchangeID])
    {
        // An update may change only the mutable fields.
        std::array<SField const*, 6> const fixed{
            &sfAsset, &sfScale, &sfDomainID, &sfVaultID, &sfVaultLossWindow, &sfParameterDelay};
        for (auto const* field : fixed)
        {
            if (tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (*exchangeID == beast::kZero)
            return temMALFORMED;
        return tesSUCCESS;
    }

    if (!tx.isFieldPresent(sfAsset) || !tx.isFieldPresent(sfParameterDelay))
        return temMALFORMED;

    if (tx[sfParameterDelay] < perp::kMinParameterDelay.count())
        return temMALFORMED;

    // A backstop vault and its loss window come together.
    if (tx.isFieldPresent(sfVaultID) != tx.isFieldPresent(sfVaultLossWindow))
        return temMALFORMED;
    if (auto const vaultID = tx[~sfVaultID];
        vaultID && (*vaultID == beast::kZero || tx[sfVaultLossWindow] == 0))
        return temMALFORMED;

    if (auto const domainID = tx[~sfDomainID]; domainID && *domainID == beast::kZero)
        return temMALFORMED;

    if (auto const scale = tx[~sfScale])
    {
        auto const asset = tx[sfAsset];
        if (asset.native() || asset.holds<MPTIssue>())
            return temMALFORMED;

        auto const maximumScale = getVaultDefaultVersion(ctx.rules) == VaultVersion::FixedPrecision
            ? kFixedPrecisionVaultMaximumIouScale
            : kLegacyVaultMaximumIouScale;
        if (*scale > maximumScale)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
PerpExchangeSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];

    if (auto const exchangeID = tx[~sfExchangeID])
    {
        auto const exchange = ctx.view.read(keylet::perpExchange(*exchangeID));
        if (!exchange)
            return tecNO_ENTRY;
        if ((*exchange)[sfOwner] != account)
            return tecNO_PERMISSION;
        return tesSUCCESS;
    }

    auto const asset = tx[sfAsset];

    // Withdrawals from the pseudo-account must not depend on a transfer waiver.
    if (asset.holds<MPTIssue>())
    {
        auto const issuance =
            ctx.view.read(keylet::mptokenIssuance(asset.get<MPTIssue>().getMptID()));
        if (issuance && !issuance->isFlag(lsfMPTCanTransfer))
            return tecNO_PERMISSION;
    }

    if (auto const ter = canAddHolding(ctx.view, asset))
        return ter;

    // An asset issued by a pseudo-account could never be clawed back.
    if (!asset.native() && isPseudoAccount(ctx.view, asset.getIssuer()))
        return tecWRONG_ASSET;

    if (isFrozen(ctx.view, account, asset))
        return asset.holds<Issue>() ? tecFROZEN : tecLOCKED;

    if (auto const domainID = tx[~sfDomainID];
        domainID && !ctx.view.exists(keylet::permissionedDomain(*domainID)))
        return tecNO_ENTRY;

    if (auto const vaultID = tx[~sfVaultID])
    {
        auto const vault = ctx.view.read(keylet::vault(*vaultID));
        if (!vault)
            return tecNO_ENTRY;
        if ((*vault)[sfOwner] != account || (*vault)[sfAsset] != asset ||
            vault->isFieldPresent(sfPerpExchangeID))
            return tecNO_PERMISSION;
    }

    if (pseudoAccountAddress(ctx.view, keylet::perpExchange(account, tx.getSeqProxy()).key) ==
        beast::kZero)
        return terADDRESS_COLLISION;

    return tesSUCCESS;
}

TER
PerpExchangeSet::doApply()
{
    if (ctx_.tx.isFieldPresent(sfExchangeID))
        return updateExchange();
    return createExchange();
}

TER
PerpExchangeSet::createExchange()
{
    auto const& tx = ctx_.tx;
    auto const sequence = tx.getSeqProxy();
    auto const owner = view().peek(keylet::account(accountID_));
    if (!owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const exchange = std::make_shared<SLE>(keylet::perpExchange(accountID_, sequence));
    if (auto const ter = dirLink(view(), accountID_, exchange))
        return ter;

    // The exchange and its pseudo-account each take an owner reserve.
    increaseOwnerCount(view(), owner, {}, 2, j_);
    if (preFeeBalance_ < accountReserve(view(), owner, j_))
        return tecINSUFFICIENT_RESERVE;

    auto const pseudo = createPseudoAccount(view(), exchange->key(), sfPerpExchangeID);
    if (!pseudo)
        return pseudo.error();  // LCOV_EXCL_LINE
    AccountID const pseudoID = (*pseudo)->at(sfAccount);
    auto const asset = tx[sfAsset];
    if (auto const ter =
            addEmptyHolding(ctx_.getApplyViewContext(), pseudoID, preFeeBalance_, asset, j_);
        !isTesSuccess(ter))
        return ter;

    (*exchange)[sfOwner] = accountID_;
    (*exchange)[sfSequence] = sequence.value();
    (*exchange)[sfAccount] = pseudoID;
    exchange->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    if (asset.holds<Issue>() && !asset.native())
        (*exchange)[sfScale] = tx[~sfScale].value_or(kVaultDefaultIouScale);
    (*exchange)[~sfDomainID] = tx[~sfDomainID];
    (*exchange)[~sfVaultID] = tx[~sfVaultID];
    (*exchange)[~sfVaultLossWindow] = tx[~sfVaultLossWindow];
    (*exchange)[sfParameterDelay] = tx[sfParameterDelay];
    for (auto const* field :
         {&sfMakerFee,
          &sfTakerFee,
          &sfOwnerFeeShare,
          &sfVaultFeeShare,
          &sfBuilderFeeMax,
          &sfCoverRateMinimum,
          &sfVaultLossLimit})
    {
        if (auto const value = tx[~*field])
            (*exchange)[*field] = *value;
    }
    if (auto const data = tx[~sfData]; data && !data->empty())
        exchange->setFieldVL(sfData, *data);

    // The backstop vault records the exchange it backs.
    if (auto const vaultID = tx[~sfVaultID])
    {
        auto const vault = view().peek(keylet::vault(*vaultID));
        if (!vault)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        (*vault)[sfPerpExchangeID] = exchange->key();
        view().update(vault);
    }

    view().insert(exchange);
    return tesSUCCESS;
}

TER
PerpExchangeSet::updateExchange()
{
    auto const& tx = ctx_.tx;
    auto const exchange = view().peek(keylet::perpExchange(tx[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // A change already due applies before a new one can replace it.
    auto const now = perp::refreshTime(view());
    perp::applyPendingParameters(*exchange, now);

    // Raising a fee or share, or lowering the cover minimum, is queued; the opposite applies now.
    STObject pending = STObject::makeInnerObject(sfPendingParameters);
    bool queued = false;
    auto const change = [&](SF_UINT32 const& field, bool queueRaise) {
        auto const proposed = tx[~field];
        if (!proposed)
            return;
        std::uint32_t const current = (*exchange)[field];
        if (*proposed == current)
            return;
        if ((*proposed > current) == queueRaise)
        {
            pending[field] = *proposed;
            queued = true;
        }
        else
        {
            (*exchange)[field] = *proposed;
        }
    };
    for (auto const* field :
         {&sfMakerFee,
          &sfTakerFee,
          &sfOwnerFeeShare,
          &sfVaultFeeShare,
          &sfBuilderFeeMax,
          &sfVaultLossLimit})
        change(*field, true);
    change(sfCoverRateMinimum, false);

    if (auto const data = tx[~sfData])
    {
        if (data->empty())
        {
            exchange->makeFieldAbsent(sfData);
        }
        else
        {
            exchange->setFieldVL(sfData, *data);
        }
    }

    // A new queued change replaces the one pending.
    if (queued)
    {
        exchange->setFieldObject(sfPendingParameters, pending);
        (*exchange)[sfParameterEffectiveTime] =
            perp::parameterEffectiveTime(now, (*exchange)[sfParameterDelay]);
    }

    auto const sharesFit = [](STLedgerEntry const& entry) {
        return entry[sfOwnerFeeShare] + entry[sfVaultFeeShare] <= perp::kRateUnity;
    };
    if (!sharesFit(*exchange) || !sharesFit(perp::eventualParameters(*exchange)))
        return tecLIMIT_EXCEEDED;

    view().update(exchange);
    return tesSUCCESS;
}

void
PerpExchangeSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpExchangeSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
