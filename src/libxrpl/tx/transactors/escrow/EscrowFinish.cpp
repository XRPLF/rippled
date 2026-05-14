#include <xrpl/tx/transactors/escrow/EscrowFinish.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/conditions/Condition.h>
#include <xrpl/conditions/Fulfillment.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
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
#include <system_error>
#include <variant>

namespace xrpl {

/** HashRouter flag indicating the crypto-condition fulfillment is invalid.
 *
 *  Set in `preflightSigValidated` (and re-set in `doApply` if the cache has
 *  expired) when `checkCondition` returns false.  When this bit is present on
 *  a transaction's flags, `doApply` immediately returns
 *  `tecCRYPTOCONDITION_ERROR` without repeating the cryptographic work.
 */
constexpr HashRouterFlags kSF_CF_INVALID = HashRouterFlags::PRIVATE5;

/** HashRouter flag indicating the crypto-condition fulfillment is valid.
 *
 *  Set in `preflightSigValidated` (and re-set in `doApply` if the cache has
 *  expired) when `checkCondition` returns true.  `doApply` reads this bit to
 *  confirm that the fulfillment was already verified before proceeding with
 *  the remaining condition checks against the escrow SLE.
 */
constexpr HashRouterFlags kSF_CF_VALID = HashRouterFlags::PRIVATE6;

//------------------------------------------------------------------------------

/** Verify that a crypto-condition fulfillment satisfies a condition.
 *
 *  Deserializes both the condition (`c`) and the fulfillment (`f`) using the
 *  `cryptoconditions` library and calls `validate`.  Returns `false` if
 *  either field fails to deserialize.  This is the single call site for all
 *  crypto-condition verification in the escrow finish pipeline; its result is
 *  cached in the `HashRouter` under `kSF_CF_VALID`/`kSF_CF_INVALID` to avoid
 *  repeating the work across multiple passes of the same transaction.
 *
 *  @param f  raw bytes of the PREIMAGE-SHA-256 fulfillment.
 *  @param c  raw bytes of the condition originally stored in the escrow SLE.
 *  @return   `true` if the fulfillment validates the condition; `false` if
 *            either field fails to deserialize or validation fails.
 */
static bool
checkCondition(Slice f, Slice c)
{
    using namespace xrpl::cryptoconditions;

    std::error_code ec;

    auto condition = Condition::deserialize(c, ec);
    if (!condition)
        return false;

    auto fulfillment = Fulfillment::deserialize(f, ec);
    if (!fulfillment)
        return false;

    return validate(*fulfillment, *condition);
}

bool
EscrowFinish::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) || ctx.rules.enabled(featureCredentials);
}

NotTEC
EscrowFinish::preflight(PreflightContext const& ctx)
{
    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
        return temMALFORMED;

    return tesSUCCESS;
}

NotTEC
EscrowFinish::preflightSigValidated(PreflightContext const& ctx)
{
    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    if (cb && fb)
    {
        auto& router = ctx.registry.get().getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags(id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!any(flags & (kSF_CF_INVALID | kSF_CF_VALID)))
        {
            if (checkCondition(*fb, *cb))
            {
                router.setFlags(id, kSF_CF_VALID);
            }
            else
            {
                router.setFlags(id, kSF_CF_INVALID);
            }
        }
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
EscrowFinish::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};

    if (auto const fb = tx[~sfFulfillment])
    {
        extraFee += view.fees().base * (32 + (fb->size() / 16));
    }

    return Transactor::calculateBaseFee(view, tx) + extraFee;
}

template <ValidIssueType T>
static TER
escrowFinishPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount);

/** IOU specialization: read-only eligibility check for an escrow destination.
 *
 *  Verifies that `dest` is permitted to receive the IOU held in escrow:
 *  - If the issuer equals the destination the check trivially passes (the
 *    issuer cannot be unauthorized or frozen against itself).
 *  - Otherwise `requireAuth` is called; if the issuer has `lsfRequireAuth`
 *    set and `dest` is not authorized on the trust line, the check fails.
 *  - Then `isDeepFrozen` is consulted; deep-freeze on the trust line blocks
 *    the finish regardless of authorization.
 *
 *  @param ctx     read-only preclaim context.
 *  @param dest    the escrow destination account.
 *  @param amount  the escrowed IOU amount (carries the issuer and currency).
 *  @return        `tesSUCCESS` if the destination may receive the asset;
 *                 `tecNO_AUTH` if `requireAuth` is set and the trust line is
 *                 unauthorized; `tecFROZEN` if the trust line is deep-frozen.
 */
template <>
TER
escrowFinishPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID const& issuer = amount.getIssuer();
    if (issuer == dest)
        return tesSUCCESS;

    if (auto const ter = requireAuth(ctx.view, amount.get<Issue>(), dest); !isTesSuccess(ter))
        return ter;

    if (isDeepFrozen(ctx.view, dest, amount.get<Issue>().currency, amount.getIssuer()))
        return tecFROZEN;

    return tesSUCCESS;
}

/** MPT specialization: read-only eligibility check for an escrow destination.
 *
 *  Verifies that `dest` is permitted to receive the MPToken held in escrow:
 *  - If the issuer equals the destination the check trivially passes.
 *  - The MPT issuance object must exist on the ledger; a missing issuance
 *    means the asset was destroyed after the escrow was created.
 *  - `requireAuth` is called with `AuthType::WeakAuth`, which requires an
 *    `MPToken` holder entry to exist but does not require the issuer to have
 *    explicitly authorized it (less strict than IOU `requireAuth`).
 *  - `isFrozen` checks for an MPT-level lock on the destination's holding.
 *
 *  @param ctx     read-only preclaim context.
 *  @param dest    the escrow destination account.
 *  @param amount  the escrowed MPT amount (carries the MPT issuance ID).
 *  @return        `tesSUCCESS` if the destination may receive the asset;
 *                 `tecOBJECT_NOT_FOUND` if the MPT issuance no longer exists;
 *                 a `requireAuth` error code if `WeakAuth` fails;
 *                 `tecLOCKED` if the destination's MPToken holding is frozen.
 */
template <>
TER
escrowFinishPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID const& issuer = amount.getIssuer();
    if (issuer == dest)
        return tesSUCCESS;

    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

    return tesSUCCESS;
}

TER
EscrowFinish::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureCredentials))
    {
        if (auto const err = credentials::valid(ctx.tx, ctx.view, ctx.tx[sfAccount], ctx.j);
            !isTesSuccess(err))
            return err;
    }

    if (ctx.view.rules().enabled(featureTokenEscrow))
    {
        auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        AccountID const dest = (*slep)[sfDestination];
        STAmount const amount = (*slep)[sfAmount];

        if (!isXRP(amount))
        {
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowFinishPreclaimHelper<T>(ctx, dest, amount);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }
    return tesSUCCESS;
}

TER
EscrowFinish::doApply()
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

    // Too soon: can't execute before the finish time
    if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
        return tecNO_PERMISSION;

    // Too late: can't execute after the cancel time
    if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.registry.get().getHashRouter().getFlags(id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && !any(flags & (kSF_CF_INVALID | kSF_CF_VALID)))
        {
            // LCOV_EXCL_START
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
            {
                flags = kSF_CF_VALID;
            }
            else
            {
                flags = kSF_CF_INVALID;
            }

            ctx_.registry.get().getHashRouter().setFlags(id, flags);
            // LCOV_EXCL_STOP
        }

        if (any(flags & kSF_CF_INVALID))
            return tecCRYPTOCONDITION_ERROR;

        auto const cond = (*slep)[~sfCondition];

        // If a condition wasn't specified during creation,
        // one shouldn't be included now.
        if (!cond && cb)
            return tecCRYPTOCONDITION_ERROR;

        // If a condition was specified during creation of
        // the suspended payment, the identical condition
        // must be presented again. We don't check if the
        // fulfillment matches the condition since we did
        // that in preflight.
        if (cond && (cond != cb))
            return tecCRYPTOCONDITION_ERROR;
    }

    // NOTE: Escrow payments cannot be used to fund accounts.
    AccountID const destID = (*slep)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (!sled)
        return tecNO_DST;

    if (auto err = verifyDepositPreauth(ctx_.tx, ctx_.view(), account_, destID, sled, ctx_.journal);
        !isTesSuccess(err))
        return err;

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

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode])
    {
        if (!ctx_.view().dirRemove(keylet::ownerDir(destID), *optPage, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    STAmount const amount = slep->getFieldAmount(sfAmount);
    if (isXRP(amount))
    {
        (*sled)[sfBalance] = (*sled)[sfBalance] + amount;
    }
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;  // LCOV_EXCL_LINE

        Rate lockedRate = slep->isFieldPresent(sfTransferRate)
            ? xrpl::Rate(slep->getFieldU32(sfTransferRate))
            : kPARITY_RATE;
        auto const issuer = amount.getIssuer();
        bool const createAsset = destID == account_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.view(),
                        lockedRate,
                        sled,
                        preFeeBalance_,
                        amount,
                        issuer,
                        account,
                        destID,
                        createAsset,
                        j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;

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

    ctx_.view().update(sled);

    auto const sle = ctx_.view().peek(keylet::account(account));
    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    ctx_.view().erase(slep);
    return tesSUCCESS;
}

void
EscrowFinish::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
EscrowFinish::finalizeInvariants(
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
