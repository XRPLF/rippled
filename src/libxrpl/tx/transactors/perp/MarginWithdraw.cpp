#include <xrpl/tx/transactors/perp/MarginWithdraw.h>

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

namespace xrpl {

std::uint32_t
MarginWithdraw::getFlagsMask(PreflightContext const& ctx)
{
    return tfMarginWithdrawMask;
}

NotTEC
MarginWithdraw::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfExchangeID] == beast::kZero)
        return temMALFORMED;

    bool const cover = tx.isFlag(tfPerpCover);
    bool const ownerFees = tx.isFlag(tfPerpOwnerFees);
    bool const deleteAccount = tx.isFlag(tfPerpDeleteAccount);
    if (static_cast<int>(cover) + static_cast<int>(ownerFees) + static_cast<int>(deleteAccount) > 1)
        return temINVALID_FLAG;

    // Only a withdrawal that deletes the account may move nothing.
    auto const amount = tx[sfAmount];
    if (amount < beast::kZero || (amount == beast::kZero && !deleteAccount) || !isLegalNet(amount))
        return temBAD_AMOUNT;

    if (auto const destination = tx[~sfDestination]; destination && *destination == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MarginWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];
    auto const amount = tx[sfAmount];
    auto const dstAcct = tx[~sfDestination].value_or(account);

    auto const exchange = ctx.view.read(keylet::perpExchange(tx[sfExchangeID]));
    if (!exchange)
        return tecNO_ENTRY;
    Asset const asset = (*exchange)[sfAsset];
    if (amount.asset() != asset)
        return tecWRONG_ASSET;
    if (isPseudoAccount(ctx.view, dstAcct))
        return tecPSEUDO_ACCOUNT;

    Number const rounded = perp::roundCollateral(*exchange, amount, Number::RoundingMode::Downward);
    if (amount != beast::kZero && rounded <= Number{})
        return tecPRECISION_LOSS;

    bool const cover = tx.isFlag(tfPerpCover);
    if (cover || tx.isFlag(tfPerpOwnerFees))
    {
        if ((*exchange)[sfOwner] != account)
            return tecNO_PERMISSION;
        if ((*exchange)[sfBadDebt] > Number{})
            return tecPERP_INSUFFICIENT_COVER;

        Number const available = cover ? (*exchange)[sfCoverAvailable] : (*exchange)[sfOwnerFees];
        if (rounded > available)
            return tecINSUFFICIENT_FUNDS;

        if (cover && available - rounded < perp::coverMinimum(*exchange))
            return tecPERP_INSUFFICIENT_COVER;
    }
    else if (!ctx.view.exists(keylet::marginAccount(account, exchange->key())))
    {
        return tecNO_ENTRY;
    }

    if (rounded == Number{})
        return tesSUCCESS;

    // The payout leaves the pseudo-account like any withdrawal from a protocol account.
    STAmount const payout{asset, rounded};
    AccountID const pseudoID = (*exchange)[sfAccount];
    if (auto const ter = canTransfer(ctx.view, asset, pseudoID, dstAcct))
        return ter;

    AuthType authType = AuthType::WeakAuth;
    if (account != dstAcct)
    {
        if (auto const ter = canWithdraw(ctx.view, account, dstAcct, payout, false))
            return ter;
        authType = AuthType::StrongAuth;
    }
    if (auto const ter = requireAuth(ctx.view, asset, dstAcct, authType))
        return ter;
    if (account == dstAcct && !holdingExists(ctx.view, dstAcct, asset))
    {
        if (auto const ter = canAddHolding(ctx.view, asset); !isTesSuccess(ter))
            return ter;
    }
    if (auto const ter = checkWithdrawFreeze(ctx.view, pseudoID, account, dstAcct, asset))
        return ter;

    return tesSUCCESS;
}

TER
MarginWithdraw::withdrawMargin(SLE::Ref exchange, Number const& amount, Number& payout)
{
    auto const& tx = ctx_.tx;
    auto const margin = view().peek(keylet::marginAccount(accountID_, exchange->key()));
    if (!margin)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ter = perp::settleAccount(view(), margin, exchange, j_); !isTesSuccess(ter))
        return ter;
    if ((*margin)[sfWaterfallCredit] > Number{})
        return tecPERP_WATERFALL_CREDIT;

    auto const available = perp::withdrawable(view(), *margin);
    if (!available)
        return available.error();
    if (amount > *available)
        return tecPERP_INSUFFICIENT_MARGIN;

    // Bad debt is recovered from the payout, not from the account's cash.
    auto const haircut = perp::withdrawalHaircut(*exchange, amount);
    if (!haircut)
        return haircut.error();
    payout = amount - *haircut;
    (*margin)[sfCash] = (*margin)[sfCash] - amount;
    (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - amount;
    (*exchange)[sfBadDebt] = (*exchange)[sfBadDebt] - *haircut;

    if (!tx.isFlag(tfPerpDeleteAccount))
    {
        associateAsset(*margin, (*exchange)[sfAsset]);
        view().update(margin);
        return tesSUCCESS;
    }

    // Positions and resting orders keep sfMarkets non-empty.
    if ((*margin)[sfCash] != Number{} || !margin->getFieldV256(sfMarkets).empty())
        return tecHAS_OBLIGATIONS;

    auto const root = view().peek(keylet::account(accountID_));
    if (!root)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (!view().dirRemove(
            keylet::ownerDir(accountID_), (*margin)[sfOwnerNode], margin->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    decreaseOwnerCountForObject(view(), root, margin, 1, j_);
    (*exchange)[sfMarginAccountCount] = (*exchange)[sfMarginAccountCount] - 1;
    view().erase(margin);
    return tesSUCCESS;
}

TER
MarginWithdraw::doApply()
{
    auto const& tx = ctx_.tx;
    auto const exchange = view().peek(keylet::perpExchange(tx[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const asset = (*exchange)[sfAsset];
    Number const amount =
        perp::roundCollateral(*exchange, tx[sfAmount], Number::RoundingMode::Downward);
    Number payout = amount;
    if (tx.isFlag(tfPerpCover))
    {
        (*exchange)[sfCoverAvailable] = (*exchange)[sfCoverAvailable] - amount;
    }
    else if (tx.isFlag(tfPerpOwnerFees))
    {
        (*exchange)[sfOwnerFees] = (*exchange)[sfOwnerFees] - amount;
    }
    else if (auto const ter = withdrawMargin(exchange, amount, payout); !isTesSuccess(ter))
    {
        return ter;
    }

    Number const assetsTotal = (*exchange)[sfAssetsTotal] - payout;
    if (!perp::holdsExactly(asset, assetsTotal))
        return tecPRECISION_LOSS;
    (*exchange)[sfAssetsTotal] = assetsTotal;
    associateAsset(*exchange, asset);
    view().update(exchange);
    if (payout == Number{})
        return tesSUCCESS;

    STAmount const paid{asset, payout};
    return doWithdraw(
        ctx_.getApplyViewContext(),
        accountID_,
        tx[~sfDestination].value_or(accountID_),
        (*exchange)[sfAccount],
        preFeeBalance_,
        paid,
        paid,
        j_);
}

void
MarginWithdraw::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
MarginWithdraw::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
