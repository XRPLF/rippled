//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
Change::preflight(PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
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

    if (ctx.tx.getSequence() != 0 || ctx.tx.isFieldPresent(sfPreviousTxnID))
    {
        JLOG(ctx.j.warn()) << "Change: Bad sequence";
        return temBAD_SEQUENCE;
    }

    if (ctx.tx.getTxnType() == ttUNL_MODIFY &&
        !ctx.rules.enabled(featureNegativeUNL))
    {
        JLOG(ctx.j.warn()) << "Change: NegativeUNL not enabled";
        return temDISABLED;
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
        case ttAMENDMENT:
        case ttFEE:
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
        default:
            assert(0);
            return tefFAILURE;
    }
}

void
Change::preCompute()
{
    account_ = ctx_.tx.getAccountID(sfAccount);
    assert(account_ == beast::zero);
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

    const bool gotMajority = (flags & tfGotMajority) != 0;
    const bool lostMajority = (flags & tfLostMajority) != 0;

    if (gotMajority && lostMajority)
        return temINVALID_FLAG;

    STArray newMajorities(sfMajorities);

    bool found = false;
    if (amendmentObject->isFieldPresent(sfMajorities))
    {
        const STArray& oldMajorities =
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
        newMajorities.push_back(STObject(sfMajority));
        auto& entry = newMajorities.back();
        entry.emplace_back(STHash256(sfAmendment, amendment));
        entry.emplace_back(STUInt32(
            sfCloseTime, view().parentCloseTime().time_since_epoch().count()));

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

    feeObject->setFieldU64(sfBaseFee, ctx_.tx.getFieldU64(sfBaseFee));
    feeObject->setFieldU32(
        sfReferenceFeeUnits, ctx_.tx.getFieldU32(sfReferenceFeeUnits));
    feeObject->setFieldU32(sfReserveBase, ctx_.tx.getFieldU32(sfReserveBase));
    feeObject->setFieldU32(
        sfReserveIncrement, ctx_.tx.getFieldU32(sfReserveIncrement));

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

    JLOG(j_.info()) << "N-UNL: applyUNLModify, disabling=" << disabling
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
        if (negUnlObject->isFieldPresent(sfNegativeUNL))
        {
            auto const& negUnl = negUnlObject->getFieldArray(sfNegativeUNL);
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
        if (negUnlObject->isFieldPresent(sfNegativeUNLToDisable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToDisable";
            return tefFAILURE;
        }

        // cannot be the same as toReEnable
        if (negUnlObject->isFieldPresent(sfNegativeUNLToReEnable))
        {
            if (negUnlObject->getFieldVL(sfNegativeUNLToReEnable) == validator)
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

        negUnlObject->setFieldVL(sfNegativeUNLToDisable, validator);
    }
    else
    {
        // cannot have more than one toReEnable
        if (negUnlObject->isFieldPresent(sfNegativeUNLToReEnable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToReEnable";
            return tefFAILURE;
        }

        // cannot be the same as toDisable
        if (negUnlObject->isFieldPresent(sfNegativeUNLToDisable))
        {
            if (negUnlObject->getFieldVL(sfNegativeUNLToDisable) == validator)
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

        negUnlObject->setFieldVL(sfNegativeUNLToReEnable, validator);
    }

    view().update(negUnlObject);
    return tesSUCCESS;
}

}  // namespace ripple
