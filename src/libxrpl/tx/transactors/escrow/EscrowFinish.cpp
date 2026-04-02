#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/conditions/Condition.h>
#include <xrpl/conditions/Fulfillment.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/transactors/escrow/EscrowFinish.h>

#include <libxrpl/tx/transactors/escrow/EscrowHelpers.h>

namespace xrpl {

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
constexpr HashRouterFlags SF_CF_INVALID = HashRouterFlags::PRIVATE5;
constexpr HashRouterFlags SF_CF_VALID = HashRouterFlags::PRIVATE6;

//------------------------------------------------------------------------------

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
    if (ctx.tx.isFieldPresent(sfCredentialIDs) && !ctx.rules.enabled(featureCredentials))
        return false;

    if (ctx.tx.isFieldPresent(sfComputationAllowance) && !ctx.rules.enabled(featureSmartEscrow))
    {
        return false;
    }
    return true;
}

NotTEC
EscrowFinish::preflight(PreflightContext const& ctx)
{
    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
    {
        JLOG(ctx.j.debug()) << "Condition != Fulfillment";
        return temMALFORMED;
    }

    if (auto const allowance = ctx.tx[~sfComputationAllowance]; allowance)
    {
        auto const fees(ctx.registry.getFees());
        if (fees.extensionComputeLimit == 0)
        {
            JLOG(ctx.j.debug()) << "WASM runtime deactivated by fee voting";
            return temTEMP_DISABLED;
        }
        if (*allowance == 0)
        {
            return temBAD_LIMIT;
        }
        if (*allowance > fees.extensionComputeLimit)
        {
            JLOG(ctx.j.debug()) << "ComputationAllowance too large: " << *allowance;
            return temBAD_LIMIT;
        }
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

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
        if (!any(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition(*fb, *cb))
            {
                router.setFlags(id, SF_CF_VALID);
            }
            else
            {
                router.setFlags(id, SF_CF_INVALID);
            }
        }
    }

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
    if (std::optional<uint64_t> const allowance = tx[~sfComputationAllowance]; allowance)
    {
        // The extra fee is the allowance in drops, rounded up to the nearest
        // whole drop.
        // Integer math rounds down by default, so we add 1 to round up.
        uint64_t const allowanceFee =
            ((*allowance) * view.fees().gasPrice) / MICRO_DROPS_PER_DROP + 1;
        extraFee += allowanceFee;
    }
    return Transactor::calculateBaseFee(view, tx) + extraFee;
}

template <ValidIssueType T>
static TER
escrowFinishPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount);

template <>
TER
escrowFinishPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID const issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == dest)
        return tesSUCCESS;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), dest); !isTesSuccess(ter))
        return ter;

    // If the issuer has deep frozen the destination, return tecFROZEN
    if (isDeepFrozen(ctx.view, dest, amount.getCurrency(), amount.getIssuer()))
        return tecFROZEN;

    return tesSUCCESS;
}

template <>
TER
escrowFinishPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID const issuer = amount.getIssuer();
    // If the issuer is the same as the dest, return tesSUCCESS
    if (issuer == dest)
        return tesSUCCESS;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the destination, return tecLOCKED
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

    if (ctx.view.rules().enabled(featureTokenEscrow) ||
        ctx.view.rules().enabled(featureSmartEscrow))
    {
        // this check is done in doApply before this amendment is enabled
        auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        if (ctx.view.rules().enabled(featureSmartEscrow))
        {
            if (slep->isFieldPresent(sfFinishFunction))
            {
                if (!ctx.tx.isFieldPresent(sfComputationAllowance))
                {
                    JLOG(ctx.j.debug()) << "FinishFunction requires ComputationAllowance";
                    return tefWASM_FIELD_NOT_INCLUDED;
                }
            }
            else
            {
                if (ctx.tx.isFieldPresent(sfComputationAllowance))
                {
                    JLOG(ctx.j.debug()) << "FinishFunction not present, "
                                           "ComputationAllowance present";
                    return tefNO_WASM;
                }
            }
        }
        if (ctx.view.rules().enabled(featureTokenEscrow))
        {
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
        if (ctx_.view().rules().enabled(featureTokenEscrow) ||
            ctx_.view().rules().enabled(featureSmartEscrow))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return tecNO_TARGET;
    }

    // If a cancel time is present, a finish operation should only succeed prior
    // to that time.
    auto const now = ctx_.view().header().parentCloseTime;

    // Too soon: can't execute before the finish time
    if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
        return tecNO_PERMISSION;

    // Too late: can't execute after the cancel time
    if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const destID = (*slep)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (ctx_.view().rules().enabled(featureSmartEscrow))
    {
        // NOTE: Escrow payments cannot be used to fund accounts.
        if (!sled)
            return tecNO_DST;

        if (auto err =
                verifyDepositPreauth(ctx_.tx, ctx_.view(), account_, destID, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.registry.get().getHashRouter().getFlags(id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && !any(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            // LCOV_EXCL_START
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
            {
                flags = SF_CF_VALID;
            }
            else
            {
                flags = SF_CF_INVALID;
            }

            ctx_.registry.get().getHashRouter().setFlags(id, flags);
            // LCOV_EXCL_STOP
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (any(flags & SF_CF_INVALID))
            return tecCRYPTOCONDITION_ERROR;

        // Check against condition in the ledger entry:
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

    if (!ctx_.view().rules().enabled(featureSmartEscrow))
    {
        // NOTE: Escrow payments cannot be used to fund accounts.
        if (!sled)
            return tecNO_DST;

        if (auto err =
                verifyDepositPreauth(ctx_.tx, ctx_.view(), account_, destID, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    // Execute custom release function
    if ((*slep)[~sfFinishFunction])
    {
        JLOG(j_.trace()) << "The escrow has a finish function, running WASM code...";
        // WASM execution
        auto const wasmStr = slep->getFieldVL(sfFinishFunction);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

        WasmHostFunctionsImpl ledgerDataProvider(ctx_, k);

        if (!ctx_.tx.isFieldPresent(sfComputationAllowance))
        {
            // already checked above, this check is just in case
            return tecINTERNAL;
        }
        std::uint32_t const allowance = ctx_.tx[sfComputationAllowance];
        auto re = runEscrowWasm(wasm, ledgerDataProvider, allowance, ESCROW_FUNCTION_NAME);
        JLOG(j_.trace()) << "Escrow WASM ran";

        if (auto const& data = ledgerDataProvider.getData(); data.has_value())
        {
            if (data->size() > maxWasmDataLength)
            {
                // should already be checked in the updateData host function
                return tecINTERNAL;  // LCOV_EXCL_LINE
            }
            slep->setFieldVL(sfData, makeSlice(*data));
            ctx_.view().update(slep);
        }

        if (re.has_value())
        {
            auto const reValue = re.value().result;
            auto const reCost = re.value().cost;
            JLOG(j_.debug()) << "WASM Success: " + std::to_string(reValue) << ", cost: " << reCost;

            ctx_.setWasmReturnCode(reValue);

            if (reCost < 0 || reCost > std::numeric_limits<uint32_t>::max())
                return tecINTERNAL;  // LCOV_EXCL_LINE
            ctx_.setGasUsed(static_cast<uint32_t>(reCost));

            if (reValue <= 0)
            {
                return tecWASM_REJECTED;
            }
        }
        else
        {
            JLOG(j_.debug()) << "WASM Failure: " + transHuman(re.error());
            return re.error();
        }
    }

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
    // Transfer amount to destination
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
            : parityRate;
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

    auto const reserveToSubtract = calculateAdditionalReserve((*slep)[~sfFinishFunction]);

    // Adjust source owner count
    auto const sle = ctx_.view().peek(keylet::account(account));
    adjustOwnerCount(ctx_.view(), sle, -1 * reserveToSubtract, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);
    return tesSUCCESS;
}

}  // namespace xrpl
