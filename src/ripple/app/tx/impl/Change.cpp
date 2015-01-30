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

#include <BeastConfig.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TER
Change::doApply()
{
    if (mTxn.getTxnType () == ttAMENDMENT)
        return applyAmendment ();

    if (mTxn.getTxnType () == ttFEE)
        return applyFee ();

    return temUNKNOWN;
}

TER
Change::checkSign()
{
    if (mTxn.getAccountID (sfAccount).isNonZero ())
    {
        j_.warning << "Bad source account";
        return temBAD_SRC_ACCOUNT;
    }

    if (!mTxn.getSigningPubKey ().empty () || !mTxn.getSignature ().empty ())
    {
        j_.warning << "Bad signature";
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

TER
Change::checkSeq()
{
    if ((mTxn.getSequence () != 0) || mTxn.isFieldPresent (sfPreviousTxnID))
    {
        j_.warning << "Bad sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

TER
Change::payFee()
{
    if (mTxn.getTransactionFee () != STAmount ())
    {
        j_.warning << "Non-zero fee";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

TER
Change::preCheck()
{
    account_ = mTxn.getAccountID(sfAccount);

    if (account_.isNonZero ())
    {
        j_.warning << "Bad source id";
        return temBAD_SRC_ACCOUNT;
    }

    if (view().open())
    {
        j_.warning << "Change transaction against open ledger";
        return temINVALID;
    }

    return tesSUCCESS;
}

TER
Change::applyAmendment()
{
    uint256 amendment (mTxn.getFieldH256 (sfAmendment));

    auto const k = keylet::amendments();

    SLE::pointer amendmentObject =
        view().peek (k);

    if (!amendmentObject)
    {
        amendmentObject = std::make_shared<SLE>(k);
        view().insert(amendmentObject);
    }

    STVector256 amendments =
        amendmentObject->getFieldV256(sfAmendments);

    if (std::find (amendments.begin(), amendments.end(),
            amendment) != amendments.end ())
        return tefALREADY;

    auto flags = mTxn.getFlags ();

    const bool gotMajority = (flags & tfGotMajority) != 0;
    const bool lostMajority = (flags & tfLostMajority) != 0;

    if (gotMajority && lostMajority)
        return temINVALID_FLAG;

    STArray newMajorities (sfMajorities);

    bool found = false;
    if (amendmentObject->isFieldPresent (sfMajorities))
    {
        const STArray &oldMajorities = amendmentObject->getFieldArray (sfMajorities);
        for (auto const& majority : oldMajorities)
        {
            if (majority.getFieldH256 (sfAmendment) == amendment)
            {
                if (gotMajority)
                    return tefALREADY;
                found = true;
            }
            else
            {
                // pass through
                newMajorities.push_back (majority);
            }
        }
    }

    if (! found && lostMajority)
        return tefALREADY;

    if (gotMajority)
    {
        // This amendment now has a majority
        newMajorities.push_back (STObject (sfMajority));
        auto& entry = newMajorities.back ();
        entry.emplace_back (STHash256 (sfAmendment, amendment));
        entry.emplace_back (STUInt32 (sfCloseTime,
            view().parentCloseTime()));
    }
    else if (!lostMajority)
    {
        // No flags, enable amendment
        amendments.push_back (amendment);
        amendmentObject->setFieldV256 (sfAmendments, amendments);

        getApp().getAmendmentTable ().enable (amendment);

        if (!getApp().getAmendmentTable ().isSupported (amendment))
            getApp().getOPs ().setAmendmentBlocked ();
    }

    if (newMajorities.empty ())
        amendmentObject->makeFieldAbsent (sfMajorities);
    else
        amendmentObject->setFieldArray (sfMajorities, newMajorities);

    view().update (amendmentObject);

    return tesSUCCESS;
}

TER
Change::applyFee()
{
    auto const k = keylet::fees();

    SLE::pointer feeObject = view().peek (k);

    if (!feeObject)
    {
        feeObject = std::make_shared<SLE>(k);
        view().insert(feeObject);
    }

    // VFALCO-FIXME this generates errors
    // j_.trace <<
    //     "Previous fee object: " << feeObject->getJson (0);

    feeObject->setFieldU64 (
        sfBaseFee, mTxn.getFieldU64 (sfBaseFee));
    feeObject->setFieldU32 (
        sfReferenceFeeUnits, mTxn.getFieldU32 (sfReferenceFeeUnits));
    feeObject->setFieldU32 (
        sfReserveBase, mTxn.getFieldU32 (sfReserveBase));
    feeObject->setFieldU32 (
        sfReserveIncrement, mTxn.getFieldU32 (sfReserveIncrement));

    view().update (feeObject);

    // VFALCO-FIXME this generates errors
    // j_.trace <<
    //     "New fee object: " << feeObject->getJson (0);
    j_.warning << "Fees have been changed";
    return tesSUCCESS;
}

}
