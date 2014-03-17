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

namespace ripple {

TER ChangeTransactor::doApply ()
{
    if (mTxn.getTxnType () == ttFEATURE)
        return applyFeature ();

    if (mTxn.getTxnType () == ttFEE)
        return applyFee ();

    return temUNKNOWN;
}

TER ChangeTransactor::checkSig ()
{
    if (mTxn.getFieldAccount160 (sfAccount).isNonZero ())
    {
        m_journal.warning << "Bad source account";
        return temBAD_SRC_ACCOUNT;
    }

    if (!mTxn.getSigningPubKey ().empty () || !mTxn.getSignature ().empty ())
    {
        m_journal.warning << "Bad signature";
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

TER ChangeTransactor::checkSeq ()
{
    if ((mTxn.getSequence () != 0) || mTxn.isFieldPresent (sfPreviousTxnID))
    {
        m_journal.warning << "Bad sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

TER ChangeTransactor::payFee ()
{
    if (mTxn.getTransactionFee () != STAmount ())
    {
        m_journal.warning << "Non-zero fee";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

TER ChangeTransactor::preCheck ()
{
    mTxnAccountID   = mTxn.getSourceAccount ().getAccountID ();

    if (mTxnAccountID.isNonZero ())
    {
        m_journal.warning << "Bad source id";

        return temBAD_SRC_ACCOUNT;
    }

    if (isSetBit (mParams, tapOPEN_LEDGER))
    {
        m_journal.warning << "Change transaction against open ledger";
        return temINVALID;
    }

    return tesSUCCESS;
}

TER ChangeTransactor::applyFeature ()
{
    uint256 feature (mTxn.getFieldH256 (sfFeature));

    SLE::pointer featureObject (mEngine->entryCache (
        ltFEATURES, Ledger::getLedgerFeatureIndex ()));

    if (!featureObject)
        featureObject = mEngine->entryCreate (
            ltFEATURES, Ledger::getLedgerFeatureIndex ());

    STVector256 features (featureObject->getFieldV256 (sfFeatures));

    if (features.hasValue (feature))
        return tefALREADY;

    features.addValue (feature);
    featureObject->setFieldV256 (sfFeatures, features);
    mEngine->entryModify (featureObject);

    getApp().getFeatureTable ().enableFeature (feature);

    if (!getApp().getFeatureTable ().isFeatureSupported (feature))
        getApp().getOPs ().setFeatureBlocked ();

    return tesSUCCESS;
}

TER ChangeTransactor::applyFee ()
{

    SLE::pointer feeObject = mEngine->entryCache (
        ltFEE_SETTINGS, Ledger::getLedgerFeeIndex ());

    if (!feeObject)
        feeObject = mEngine->entryCreate (
            ltFEE_SETTINGS, Ledger::getLedgerFeeIndex ());

    m_journal.info << 
        "Previous fee object: " << feeObject->getJson (0);

    feeObject->setFieldU64 (
        sfBaseFee, mTxn.getFieldU64 (sfBaseFee));
    feeObject->setFieldU32 (
        sfReferenceFeeUnits, mTxn.getFieldU32 (sfReferenceFeeUnits));
    feeObject->setFieldU32 (
        sfReserveBase, mTxn.getFieldU32 (sfReserveBase));
    feeObject->setFieldU32 (
        sfReserveIncrement, mTxn.getFieldU32 (sfReserveIncrement));

    mEngine->entryModify (feeObject);

    m_journal.info << 
        "New fee object: " << feeObject->getJson (0);
    m_journal.warning << "Fees have been changed";
    return tesSUCCESS;
}

}
