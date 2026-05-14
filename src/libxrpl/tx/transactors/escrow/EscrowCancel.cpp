/** @file
 *  Implementation of the `EscrowCancel` transactor.
 *
 *  Handles the expiry-refund path of the escrow lifecycle: once the ledger's
 *  parent close time has advanced past `sfCancelAfter`, any party may submit
 *  an `EscrowCancel` to return the locked funds to the original escrow creator.
 *  Supports XRP escrows (original design) and token escrows (IOU / MPT) gated
 *  behind `featureTokenEscrow`.
 */
#include <xrpl/tx/transactors/escrow/EscrowCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <variant>

namespace xrpl {

NotTEC
EscrowCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

/** Read-only authorization check for non-XRP escrow cancellations.
 *
 *  Primary template — no body; only the `Issue` and `MPTIssue` full
 *  specializations are defined. Called via `std::visit` on the `Asset` variant
 *  held by the escrow's `sfAmount`.
 *
 *  @tparam T Asset type; must be `Issue` (IOU) or `MPTIssue` (MPT).
 *  @param ctx     Read-only preclaim context providing ledger access.
 *  @param account Escrow creator (`sfAccount` on the escrow SLE).
 *  @param amount  Escrowed token amount; its asset identifies the issuer.
 *  @return `tesSUCCESS` if cancellation may proceed; a `tec` error otherwise.
 */
template <ValidIssueType T>
static TER
escrowCancelPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount);

/** IOU specialization: verifies the escrow owner is still authorized by the issuer.
 *
 *  Authorization must remain valid at cancel time, not just at creation time.
 *  The `issuer == account` case is impossible by construction but is guarded
 *  defensively and marked unreachable for coverage purposes.
 *
 *  @param ctx     Read-only preclaim context.
 *  @param account Escrow creator (`sfAccount` on the escrow SLE).
 *  @param amount  Escrowed IOU amount.
 *  @return `tesSUCCESS` if authorized; `tecINTERNAL` if issuer equals account
 *          (unreachable); a `requireAuth` error if authorization was revoked.
 */
template <>
TER
escrowCancelPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID const& issuer = amount.getIssuer();
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ter = requireAuth(ctx.view, amount.get<Issue>(), account); !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

/** MPT specialization: verifies the MPT issuance still exists and the escrow
 *  owner remains authorized to hold it.
 *
 *  Unlike the IOU path, an MPT issuance can be deleted while an escrow
 *  referencing it is still open; this check surfaces that condition early.
 *  Authorization uses `AuthType::WeakAuth` because MPTs operate under a less
 *  strict per-transaction approval model than IOU trust lines.
 *
 *  @param ctx     Read-only preclaim context.
 *  @param account Escrow creator (`sfAccount` on the escrow SLE).
 *  @param amount  Escrowed MPT amount.
 *  @return `tesSUCCESS` if authorized; `tecINTERNAL` if issuer equals account
 *          (unreachable); `tecOBJECT_NOT_FOUND` if the MPT issuance was
 *          deleted after the escrow was created; a `requireAuth` error if
 *          weak authorization is denied.
 */
template <>
TER
escrowCancelPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID const issuer = amount.getIssuer();
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

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

    // Escrows with no sfCancelAfter can never be cancelled.
    if (!(*slep)[~sfCancelAfter])
        return tecNO_PERMISSION;

    if (!after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const account = (*slep)[sfAccount];

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

    if (isXRP(amount))
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] + amount;
    }
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
                        kPARITY_RATE,
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

        // Non-XRP escrows also carry an sfIssuerNode directory entry.
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

    ctx_.view().erase(slep);

    return tesSUCCESS;
}

void
EscrowCancel::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
EscrowCancel::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}
}  // namespace xrpl
