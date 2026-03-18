#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/transactors/escrow/EscrowCancel.h>

#include <libxrpl/tx/transactors/escrow/EscrowHelpers.h>

namespace xrpl {

NotTEC
EscrowCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowCancelPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount);

template <>
TER
escrowCancelPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecINTERNAL
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), account); !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

template <>
TER
escrowCancelPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecINTERNAL
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

TER
EscrowCancel::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureTokenEscrow))
    {
        auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        AccountID const account = (*slep)[sfAccount];
        STAmount const amount = (*slep)[sfAmount];

        if (!isXRP(amount))
        {
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowCancelPreclaimHelper<T>(ctx, account, amount);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }
    return tesSUCCESS;
}

TER
EscrowCancel::doApply()
{
    auto const k = keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
    {
        if (ctx_.view().rules().enabled(featureTokenEscrow))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return tecNO_TARGET;
    }

    auto const now = ctx_.view().header().parentCloseTime;

    // No cancel time specified: can't execute at all.
    if (!(*slep)[~sfCancelAfter])
        return tecNO_PERMISSION;

    // Too soon: can't execute before the cancel time.
    if (!after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(keylet::ownerDir(account), page, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode]; optPage)
    {
        if (!ctx_.view().dirRemove(keylet::ownerDir((*slep)[sfDestination]), *optPage, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    auto const sle = ctx_.view().peek(keylet::account(account));
    STAmount const amount = slep->getFieldAmount(sfAmount);

    // Transfer amount back to the owner
    if (isXRP(amount))
        (*sle)[sfBalance] = (*sle)[sfBalance] + amount;
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;  // LCOV_EXCL_LINE

        auto const issuer = amount.getIssuer();
        bool const createAsset = account == account_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.view(),
                        parityRate,
                        slep,
                        preFeeBalance_,
                        amount,
                        issuer,
                        account,  // sender and receiver are the same
                        account,
                        createAsset,
                        j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;  // LCOV_EXCL_LINE

        // Remove escrow from issuers owner directory, if present.
        if (auto const optPage = (*slep)[~sfIssuerNode]; optPage)
        {
            if (!ctx_.view().dirRemove(keylet::ownerDir(issuer), *optPage, k.key, true))
            {
                // LCOV_EXCL_START
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
        }
    }

    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

}  // namespace xrpl
