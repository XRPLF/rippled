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
#include <xrpl/ledger/entries/EscrowEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>
#include <system_error>
#include <variant>

namespace xrpl {

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
constexpr HashRouterFlags kSfCfInvalid = HashRouterFlags::PRIVATE5;
constexpr HashRouterFlags kSfCfValid = HashRouterFlags::PRIVATE6;

//------------------------------------------------------------------------------

static bool
checkCondition(Slice f, Slice c)
{
    using namespace xrpl::cryptoconditions;

    std::error_code ec;

    auto const condition = Condition::deserialize(c, ec);
    if (!condition)
        return false;

    auto const fulfillment = Fulfillment::deserialize(f, ec);
    if (!fulfillment)
        return false;

    return validate(*fulfillment, *condition);
}

bool
EscrowFinish::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfCredentialIDs) && !ctx.rules.enabled(featureCredentials))
        return false;

    if (ctx.tx.isFieldPresent(sfGas) && !ctx.rules.enabled(featureSmartEscrow))
        return false;

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

    if (auto const allowance = ctx.tx[~sfGas]; allowance)
    {
        // Protocol ceiling only; `preflight` has no view.
        if (*allowance == 0)
        {
            return temBAD_LIMIT;
        }
        if (*allowance > kMaxGasLimit)
        {
            JLOG(ctx.j.debug()) << "Gas too large: " << *allowance;
            return temBAD_LIMIT;
        }
    }

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
        auto flags = router.getFlags(id);

        // If we haven't checked the condition, check it
        // now. Other than a malformed condition, whether
        // it passes or not isn't important in preflight.
        if (!any(flags & (kSfCfInvalid | kSfCfValid)))
        {
            if (checkCondition(*fb, *cb))
            {
                router.setFlags(id, kSfCfValid);
                flags = kSfCfValid;
            }
            else
            {
                router.setFlags(id, kSfCfInvalid);
                flags = kSfCfInvalid;
            }
        }

        // The invalid flag also covers a bad fulfillment, so
        // check the condition itself. This runs whether or not
        // the flag was cached, so the result is the same on
        // every node.
        if (any(flags & kSfCfInvalid) && ctx.rules.enabled(fixCleanup3_5_0))
        {
            using namespace xrpl::cryptoconditions;

            std::error_code ec;

            auto const condition = Condition::deserialize(*cb, ec);
            if (!condition)
            {
                JLOG(ctx.j.debug()) << "Malformed condition during escrow finish: " << ec.message();
                return temMALFORMED;
            }
        }
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.rules, ctx.j); !isTesSuccess(err))
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
    if (auto const allowance = tx[~sfGas]; allowance)
    {
        // The extra fee is the allowance in drops, rounded up to the nearest
        // whole drop. Widen before multiplying: both factors are 32-bit.
        std::uint64_t const allowanceFee =
            ((std::uint64_t{*allowance} * view.fees().gasPrice) + microDropsPerDrop - 1) /
            microDropsPerDrop;
        extraFee += allowanceFee;
    }
    return Transactor::calculateBaseFee(view, tx) + extraFee;
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
        auto const seqProxy = SeqProxy::rawSequence(ctx.tx[sfOfferSequence]);
        EscrowEntryR const escrow(ctx.tx[sfOwner], seqProxy, ctx.view);
        if (!escrow)
            return tecNO_TARGET;

        if (ctx.view.rules().enabled(featureSmartEscrow))
        {
            if (escrow->isFieldPresent(sfBytecode))
            {
                if (!ctx.tx.isFieldPresent(sfGas))
                {
                    JLOG(ctx.j.debug()) << "Bytecode requires Gas";
                    return tefBYTECODE_NOT_INCLUDED;
                }

                // The limits are voted, so they can change between ledgers.
                // A `tef`, unlike a `tem`, does not mark the transaction bad
                // for the hold time.
                auto const& fees = ctx.view.fees();
                if (isBytecodeExecutionDisabled(fees))
                {
                    JLOG(ctx.j.debug()) << "WASM runtime deactivated by fee voting";
                    return tefTEMP_DISABLED;
                }

                if (ctx.tx[sfGas] > fees.gasLimit)
                {
                    JLOG(ctx.j.debug()) << "Gas over voted limit: " << ctx.tx[sfGas];
                    return tefEXCEEDS_LIMIT;
                }
            }
            else
            {
                if (ctx.tx.isFieldPresent(sfGas))
                {
                    JLOG(ctx.j.debug()) << "Bytecode not present, "
                                           "Gas present";
                    return tefNO_BYTECODE;
                }
            }
        }
        if (ctx.view.rules().enabled(featureTokenEscrow))
        {
            AccountID const dest = (*escrow)[sfDestination];
            STAmount const amount = (*escrow)[sfAmount];

            if (!isXRP(amount))
            {
                if (auto const ret = std::visit(
                        [&]<typename T>(T const&) {
                            return escrowUnlockPreclaimHelper<T>(ctx.view, dest, amount);
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
    auto const seqProxy = SeqProxy::rawSequence(ctx_.tx[sfOfferSequence]);
    EscrowEntryW escrow(ctx_.tx[sfOwner], seqProxy, ctx_.view(), j_);
    if (!escrow)
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
    if ((*escrow)[~sfFinishAfter] && !after(now, (*escrow)[sfFinishAfter]))
        return tecNO_PERMISSION;

    // Too late: can't execute after the cancel time
    if ((*escrow)[~sfCancelAfter] && after(now, (*escrow)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const destID = (*escrow)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (ctx_.view().rules().enabled(featureSmartEscrow))
    {
        // NOTE: Escrow payments cannot be used to fund accounts.
        if (!sled)
            return tecNO_DST;

        if (auto err =
                verifyDepositPreauth(ctx_.tx, ctx_.view(), accountID_, destID, sled, ctx_.journal);
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
        if (cb && !any(flags & (kSfCfInvalid | kSfCfValid)))
        {
            // LCOV_EXCL_START
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
            {
                flags = kSfCfValid;
            }
            else
            {
                flags = kSfCfInvalid;
            }

            ctx_.registry.get().getHashRouter().setFlags(id, flags);
            // LCOV_EXCL_STOP
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (any(flags & kSfCfInvalid))
            return tecCRYPTOCONDITION_ERROR;

        // Check against condition in the ledger entry:
        auto const cond = (*escrow)[~sfCondition];

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
                verifyDepositPreauth(ctx_.tx, ctx_.view(), accountID_, destID, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    // Execute custom release function
    if ((*escrow)[~sfBytecode])
    {
        // TODO(SmartEscrow): run the escrow's contract here once the WASM
        // engine lands. That change also brings with it the reporting the run
        // produces - the gas it consumed and the code it returned, in the
        // transaction metadata - and the tecBYTECODE_REJECTED result that lets
        // a rejected escrow survive with the data the contract wrote.
        //
        // Until then an escrow carrying bytecode can be created, funded and
        // cancelled, but not finished. Reachable only under featureSmartEscrow,
        // which is Supported::No.
        JLOG(j_.debug()) << "EscrowFinish: no WASM engine to run the escrow's bytecode";
        return tecFAILED_PROCESSING;
    }

    AccountID const account = (*escrow)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*escrow)[sfOwnerNode];
        if (!ctx_.view().dirRemove(keylet::ownerDir(account), page, escrow.key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*escrow)[~sfDestinationNode])
    {
        if (!ctx_.view().dirRemove(keylet::ownerDir(destID), *optPage, escrow.key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    auto const reserveToSubtract = calculateAdditionalReserve((*escrow)[~sfBytecode]);

    // Delivery can auto-create the destination's holding; the removed escrow
    // must not be counted against its reserve. The two share a reserve payer
    // for a self-escrow, or when one sponsor covers both.
    bool const recycleReserve =
        ctx_.view().rules().enabled(featureSponsor) || ctx_.view().rules().enabled(fixCleanup3_4_0);
    if (recycleReserve)
    {
        decreaseOwnerCountForObject(
            ctx_.view(), account, escrow.mutableRawSle(), reserveToSubtract, ctx_.journal);
    }

    STAmount const amount = escrow->getFieldAmount(sfAmount);
    // Transfer amount to destination
    if (isXRP(amount))
    {
        (*sled)[sfBalance] = (*sled)[sfBalance] + amount;
    }
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;  // LCOV_EXCL_LINE

        Rate lockedRate = escrow->isFieldPresent(sfTransferRate)
            ? xrpl::Rate(escrow->getFieldU32(sfTransferRate))
            : kParityRate;
        auto const issuer = amount.getIssuer();
        bool const createAsset = destID == accountID_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.getApplyViewContext(),
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
        if (auto const optPage = (*escrow)[~sfIssuerNode]; optPage)
        {
            if (!ctx_.view().dirRemove(keylet::ownerDir(issuer), *optPage, escrow.key(), true))
            {
                // LCOV_EXCL_START
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
        }
    }

    ctx_.view().update(sled);

    if (!recycleReserve)
    {
        decreaseOwnerCountForObject(
            ctx_.view(), account, escrow.mutableRawSle(), reserveToSubtract, ctx_.journal);
    }

    // Remove escrow from ledger
    escrow.erase();
    return tesSUCCESS;
}

void
EscrowFinish::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
