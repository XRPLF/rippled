#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/tx/detail/Change.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>

#include <string_view>

namespace ripple {

template <>
NotTEC
Transactor::invokePreflight<Change>(PreflightContext const& ctx)
{
    // 0 means "Allow any flags"
    // The check for tfChangeMask is gated by LendingProtocol because that
    // feature introduced this parameter, and it's not worth adding another
    // amendment just for this.
    if (auto const ret = preflight0(
            ctx, ctx.rules.enabled(featureLendingProtocol) ? tfChangeMask : 0))
        return ret;

    auto account = ctx.tx.getAccountID(sfAccount);
    if (account != beast::zero)
    {
        JLOG(ctx.j.warn()) << "Change: Bad source id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount(sfFee);
    if (!fee.native() || fee != beast::zero)
    {
        JLOG(ctx.j.warn()) << "Change: invalid fee";
        return temBAD_FEE;
    }

    if (!ctx.tx.getSigningPubKey().empty() || !ctx.tx.getSignature().empty() ||
        ctx.tx.isFieldPresent(sfSigners))
    {
        JLOG(ctx.j.warn()) << "Change: Bad signature";
        return temBAD_SIGNATURE;
    }

    if (ctx.tx.getFieldU32(sfSequence) != 0 ||
        ctx.tx.isFieldPresent(sfPreviousTxnID))
    {
        JLOG(ctx.j.warn()) << "Change: Bad sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

TER
Change::preclaim(PreclaimContext const& ctx)
{
    // If tapOPEN_LEDGER is resurrected into ApplyFlags,
    // this block can be moved to preflight.
    if (ctx.view.open())
    {
        JLOG(ctx.j.warn()) << "Change transaction against open ledger";
        return temINVALID;
    }

    switch (ctx.tx.getTxnType())
    {
        case ttFEE:
            if (ctx.view.rules().enabled(featureXRPFees))
            {
                // The ttFEE transaction format defines these fields as
                // optional, but once the XRPFees feature is enabled, they are
                // required.
                if (!ctx.tx.isFieldPresent(sfBaseFeeDrops) ||
                    !ctx.tx.isFieldPresent(sfReserveBaseDrops) ||
                    !ctx.tx.isFieldPresent(sfReserveIncrementDrops))
                    return temMALFORMED;
                // The ttFEE transaction format defines these fields as
                // optional, but once the XRPFees feature is enabled, they are
                // forbidden.
                if (ctx.tx.isFieldPresent(sfBaseFee) ||
                    ctx.tx.isFieldPresent(sfReferenceFeeUnits) ||
                    ctx.tx.isFieldPresent(sfReserveBase) ||
                    ctx.tx.isFieldPresent(sfReserveIncrement))
                    return temMALFORMED;
            }
            else
            {
                // The ttFEE transaction format formerly defined these fields
                // as required. When the XRPFees feature was implemented, they
                // were changed to be optional. Until the feature has been
                // enabled, they are required.
                if (!ctx.tx.isFieldPresent(sfBaseFee) ||
                    !ctx.tx.isFieldPresent(sfReferenceFeeUnits) ||
                    !ctx.tx.isFieldPresent(sfReserveBase) ||
                    !ctx.tx.isFieldPresent(sfReserveIncrement))
                    return temMALFORMED;
                // The ttFEE transaction format defines these fields as
                // optional, but without the XRPFees feature, they are
                // forbidden.
                if (ctx.tx.isFieldPresent(sfBaseFeeDrops) ||
                    ctx.tx.isFieldPresent(sfReserveBaseDrops) ||
                    ctx.tx.isFieldPresent(sfReserveIncrementDrops))
                    return temDISABLED;
            }
            return tesSUCCESS;
        case ttAMENDMENT:
        case ttUNL_MODIFY:
            return tesSUCCESS;
        default:
            return temUNKNOWN;
    }
}

TER
Change::doApply()
{
    switch (ctx_.tx.getTxnType())
    {
        case ttAMENDMENT:
            return applyAmendment();
        case ttFEE:
            return applyFee();
        case ttUNL_MODIFY:
            return applyUNLModify();
        // LCOV_EXCL_START
        default:
            UNREACHABLE("ripple::Change::doApply : invalid transaction type");
            return tefFAILURE;
            // LCOV_EXCL_STOP
    }
}

void
Change::preCompute()
{
    XRPL_ASSERT(
        account_ == beast::zero, "ripple::Change::preCompute : zero account");
}

TER
Change::applyAmendment()
{
    uint256 amendment(ctx_.tx.getFieldH256(sfAmendment));

    auto const k = keylet::amendments();

    SLE::pointer amendmentObject = view().peek(k);

    if (!amendmentObject)
    {
        amendmentObject = std::make_shared<SLE>(k);
        view().insert(amendmentObject);
    }

    STVector256 amendments = amendmentObject->getFieldV256(sfAmendments);

    if (std::find(amendments.begin(), amendments.end(), amendment) !=
        amendments.end())
        return tefALREADY;

    auto flags = ctx_.tx.getFlags();

    bool const gotMajority = (flags & tfGotMajority) != 0;
    bool const lostMajority = (flags & tfLostMajority) != 0;

    if (gotMajority && lostMajority)
        return temINVALID_FLAG;

    STArray newMajorities(sfMajorities);

    bool found = false;
    if (amendmentObject->isFieldPresent(sfMajorities))
    {
        STArray const& oldMajorities =
            amendmentObject->getFieldArray(sfMajorities);
        for (auto const& majority : oldMajorities)
        {
            if (majority.getFieldH256(sfAmendment) == amendment)
            {
                if (gotMajority)
                    return tefALREADY;
                found = true;
            }
            else
            {
                // pass through
                newMajorities.push_back(majority);
            }
        }
    }

    if (!found && lostMajority)
        return tefALREADY;

    if (gotMajority)
    {
        // This amendment now has a majority
        newMajorities.push_back(STObject::makeInnerObject(sfMajority));
        auto& entry = newMajorities.back();
        entry[sfAmendment] = amendment;
        entry[sfCloseTime] =
            view().parentCloseTime().time_since_epoch().count();

        if (!ctx_.app.getAmendmentTable().isSupported(amendment))
        {
            JLOG(j_.warn()) << "Unsupported amendment " << amendment
                            << " received a majority.";
        }
    }
    else if (!lostMajority)
    {
        // No flags, enable amendment
        amendments.push_back(amendment);
        amendmentObject->setFieldV256(sfAmendments, amendments);

        ctx_.app.getAmendmentTable().enable(amendment);

        if (!ctx_.app.getAmendmentTable().isSupported(amendment))
        {
            JLOG(j_.error()) << "Unsupported amendment " << amendment
                             << " activated: server blocked.";
            ctx_.app.getOPs().setAmendmentBlocked();
        }
    }

    if (newMajorities.empty())
        amendmentObject->makeFieldAbsent(sfMajorities);
    else
        amendmentObject->setFieldArray(sfMajorities, newMajorities);

    view().update(amendmentObject);

    return tesSUCCESS;
}

TER
Change::applyFee()
{
    auto const k = keylet::fees();

    SLE::pointer feeObject = view().peek(k);

    if (!feeObject)
    {
        feeObject = std::make_shared<SLE>(k);
        view().insert(feeObject);
    }
    auto set = [](SLE::pointer& feeObject, STTx const& tx, auto const& field) {
        feeObject->at(field) = tx[field];
    };
    if (view().rules().enabled(featureXRPFees))
    {
        set(feeObject, ctx_.tx, sfBaseFeeDrops);
        set(feeObject, ctx_.tx, sfReserveBaseDrops);
        set(feeObject, ctx_.tx, sfReserveIncrementDrops);
        // Ensure the old fields are removed
        feeObject->makeFieldAbsent(sfBaseFee);
        feeObject->makeFieldAbsent(sfReferenceFeeUnits);
        feeObject->makeFieldAbsent(sfReserveBase);
        feeObject->makeFieldAbsent(sfReserveIncrement);
    }
    else
    {
        set(feeObject, ctx_.tx, sfBaseFee);
        set(feeObject, ctx_.tx, sfReferenceFeeUnits);
        set(feeObject, ctx_.tx, sfReserveBase);
        set(feeObject, ctx_.tx, sfReserveIncrement);
    }

    view().update(feeObject);

    JLOG(j_.warn()) << "Fees have been changed";
    return tesSUCCESS;
}

TER
Change::applyUNLModify()
{
    if (!isFlagLedger(view().seq()))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, not a flag ledger, seq="
                        << view().seq();
        return tefFAILURE;
    }

    if (!ctx_.tx.isFieldPresent(sfUNLModifyDisabling) ||
        ctx_.tx.getFieldU8(sfUNLModifyDisabling) > 1 ||
        !ctx_.tx.isFieldPresent(sfLedgerSequence) ||
        !ctx_.tx.isFieldPresent(sfUNLModifyValidator))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, wrong Tx format.";
        return tefFAILURE;
    }

    bool const disabling = ctx_.tx.getFieldU8(sfUNLModifyDisabling);
    auto const seq = ctx_.tx.getFieldU32(sfLedgerSequence);
    if (seq != view().seq())
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, wrong ledger seq=" << seq;
        return tefFAILURE;
    }

    Blob const validator = ctx_.tx.getFieldVL(sfUNLModifyValidator);
    if (!publicKeyType(makeSlice(validator)))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, bad validator key";
        return tefFAILURE;
    }

    JLOG(j_.info()) << "N-UNL: applyUNLModify, "
                    << (disabling ? "ToDisable" : "ToReEnable")
                    << " seq=" << seq
                    << " validator data:" << strHex(validator);

    auto const k = keylet::negativeUNL();
    SLE::pointer negUnlObject = view().peek(k);
    if (!negUnlObject)
    {
        negUnlObject = std::make_shared<SLE>(k);
        view().insert(negUnlObject);
    }

    bool const found = [&] {
        if (negUnlObject->isFieldPresent(sfDisabledValidators))
        {
            auto const& negUnl =
                negUnlObject->getFieldArray(sfDisabledValidators);
            for (auto const& v : negUnl)
            {
                if (v.isFieldPresent(sfPublicKey) &&
                    v.getFieldVL(sfPublicKey) == validator)
                    return true;
            }
        }
        return false;
    }();

    if (disabling)
    {
        // cannot have more than one toDisable
        if (negUnlObject->isFieldPresent(sfValidatorToDisable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToDisable";
            return tefFAILURE;
        }

        // cannot be the same as toReEnable
        if (negUnlObject->isFieldPresent(sfValidatorToReEnable))
        {
            if (negUnlObject->getFieldVL(sfValidatorToReEnable) == validator)
            {
                JLOG(j_.warn())
                    << "N-UNL: applyUNLModify, ToDisable is same as ToReEnable";
                return tefFAILURE;
            }
        }

        // cannot be in negative UNL already
        if (found)
        {
            JLOG(j_.warn())
                << "N-UNL: applyUNLModify, ToDisable already in negative UNL";
            return tefFAILURE;
        }

        negUnlObject->setFieldVL(sfValidatorToDisable, validator);
    }
    else
    {
        // cannot have more than one toReEnable
        if (negUnlObject->isFieldPresent(sfValidatorToReEnable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToReEnable";
            return tefFAILURE;
        }

        // cannot be the same as toDisable
        if (negUnlObject->isFieldPresent(sfValidatorToDisable))
        {
            if (negUnlObject->getFieldVL(sfValidatorToDisable) == validator)
            {
                JLOG(j_.warn())
                    << "N-UNL: applyUNLModify, ToReEnable is same as ToDisable";
                return tefFAILURE;
            }
        }

        // must be in negative UNL
        if (!found)
        {
            JLOG(j_.warn())
                << "N-UNL: applyUNLModify, ToReEnable is not in negative UNL";
            return tefFAILURE;
        }

        negUnlObject->setFieldVL(sfValidatorToReEnable, validator);
    }

    view().update(negUnlObject);
    return tesSUCCESS;
}

}  // namespace ripple
