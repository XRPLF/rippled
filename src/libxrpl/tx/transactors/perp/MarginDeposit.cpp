#include <xrpl/tx/transactors/perp/MarginDeposit.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

bool
MarginDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
MarginDeposit::getFlagsMask(PreflightContext const& ctx)
{
    return tfMarginDepositMask;
}

NotTEC
MarginDeposit::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfExchangeID] == beast::kZero)
        return temMALFORMED;

    auto const amount = tx[sfAmount];
    if (amount <= beast::kZero || !isLegalNet(amount))
        return temBAD_AMOUNT;

    // A cover deposit touches no margin account.
    if (tx.isFlag(tfPerpCover) && tx.isFieldPresent(sfSubaccount))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MarginDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];
    auto const amount = tx[sfAmount];

    auto const exchange = ctx.view.read(keylet::perpExchange(tx[sfExchangeID]));
    if (!exchange)
        return tecNO_ENTRY;
    Asset const asset = (*exchange)[sfAsset];
    if (amount.asset() != asset)
        return tecWRONG_ASSET;

    // Cover comes from the exchange owner; trader deposits are gated by the domain.
    if (tx.isFlag(tfPerpCover))
    {
        if ((*exchange)[sfOwner] != account)
            return tecNO_PERMISSION;
    }
    else if (auto const domainID = (*exchange)[~sfDomainID])
    {
        if (auto const ter = perp::checkDomain(ctx.view, *domainID, account); !isTesSuccess(ter))
            return ter;
    }

    AccountID const pseudoID = (*exchange)[sfAccount];
    if (auto const ter = canTransfer(ctx.view, asset, account, pseudoID))
        return ter;
    if (auto const ter = checkDepositFreeze(ctx.view, account, pseudoID, asset))
        return ter;
    if (auto const ter = requireAuth(ctx.view, asset, account, AuthType::StrongAuth))
        return ter;

    Number const rounded = perp::roundCollateral(*exchange, amount, Number::RoundingMode::Downward);
    if (rounded <= Number{})
        return tecPRECISION_LOSS;

    if (accountHolds(
            ctx.view,
            account,
            asset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j) < STAmount{asset, rounded})
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
MarginDeposit::doApply()
{
    auto const& tx = ctx_.tx;
    auto const exchange = view().peek(keylet::perpExchange(tx[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const asset = (*exchange)[sfAsset];
    AccountID const pseudoID = (*exchange)[sfAccount];
    Number const value =
        perp::roundCollateral(*exchange, tx[sfAmount], Number::RoundingMode::Downward);
    STAmount const amount{asset, value};

    // Cover clears bad debt first; the rest is the owner's first-loss cover.
    if (tx.isFlag(tfPerpCover))
    {
        if (auto const ter =
                accountSend(view(), accountID_, pseudoID, amount, j_, {}, WaiveTransferFee::Yes))
            return ter;
        (*exchange)[sfCoverAvailable] =
            (*exchange)[sfCoverAvailable] + perp::absorbBadDebt(*exchange, value);
        (*exchange)[sfAssetsTotal] = (*exchange)[sfAssetsTotal] + value;
        associateAsset(*exchange, asset);
        view().update(exchange);
        return tesSUCCESS;
    }

    // Deletes expired credentials before any other change.
    if (auto const domainID = (*exchange)[~sfDomainID])
    {
        if (auto const ter = perp::verifyDomain(view(), *domainID, accountID_, j_);
            !isTesSuccess(ter))
            return ter;
    }

    auto const keylet =
        keylet::marginAccount(accountID_, exchange->key(), tx[~sfSubaccount].value_or(0));
    auto account = view().peek(keylet);
    bool const created = !account;
    if (created)
    {
        auto const owner = view().peek(keylet::account(accountID_));
        if (!owner)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        account = std::make_shared<SLE>(keylet);
        if (auto const ter = dirLink(view(), accountID_, account))
            return ter;
        increaseOwnerCount(view(), owner, {}, 1, j_);
        if (preFeeBalance_ < accountReserve(view(), owner, j_))
            return tecINSUFFICIENT_RESERVE;

        (*account)[sfAccount] = accountID_;
        (*account)[sfExchangeID] = exchange->key();
        (*account)[sfSubaccount] = tx[~sfSubaccount].value_or(0);
        (*exchange)[sfMarginAccountCount] = (*exchange)[sfMarginAccountCount] + 1;
    }
    else if (
        auto const ter = perp::settleAccount(view(), account, exchange, j_); !isTesSuccess(ter))
    {
        return ter;
    }

    // An XRP deposit must leave the reserve, including a new account's, in place.
    if (asset.native() &&
        accountHolds(
            view(),
            accountID_,
            asset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            j_) < amount)
        return tecINSUFFICIENT_FUNDS;

    if (auto const ter =
            accountSend(view(), accountID_, pseudoID, amount, j_, {}, WaiveTransferFee::Yes))
        return ter;

    (*account)[sfCash] = (*account)[sfCash] + value;
    (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] + value;
    (*exchange)[sfAssetsTotal] = (*exchange)[sfAssetsTotal] + value;
    associateAsset(*account, asset);
    associateAsset(*exchange, asset);
    if (created)
    {
        view().insert(account);
    }
    else
    {
        view().update(account);
    }
    view().update(exchange);
    return tesSUCCESS;
}

void
MarginDeposit::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
MarginDeposit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
