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

SETUP_LOG (Transactor)

std::unique_ptr<Transactor> Transactor::makeTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine)
{
    switch (txn.getTxnType ())
    {
    case ttPAYMENT:
        return std::unique_ptr<Transactor> (new PaymentTransactor (txn, params, engine));

    case ttACCOUNT_SET:
        return std::unique_ptr<Transactor> (new AccountSetTransactor (txn, params, engine));

    case ttREGULAR_KEY_SET:
        return std::unique_ptr<Transactor> (new RegularKeySetTransactor (txn, params, engine));

    case ttTRUST_SET:
        return std::unique_ptr<Transactor> (new TrustSetTransactor (txn, params, engine));

    case ttOFFER_CREATE:
        return std::unique_ptr<Transactor> (new OfferCreateTransactor (txn, params, engine));

    case ttOFFER_CANCEL:
        return std::unique_ptr<Transactor> (new OfferCancelTransactor (txn, params, engine));

    case ttWALLET_ADD:
        return std::unique_ptr<Transactor> (new WalletAddTransactor (txn, params, engine));

    case ttFEATURE:
    case ttFEE:
        return std::unique_ptr<Transactor> (new ChangeTransactor (txn, params, engine));

    default:
        return std::unique_ptr<Transactor> ();
    }
}


Transactor::Transactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : mTxn (txn), mEngine (engine), mParams (params)
{
    mHasAuthKey = false;
    mSigMaster = false;
}

void Transactor::calculateFee ()
{
    mFeeDue = STAmount (mEngine->getLedger ()->scaleFeeLoad (calculateBaseFee (), isSetBit (mParams, tapADMIN)));
}

uint64 Transactor::calculateBaseFee ()
{
    return getConfig ().FEE_DEFAULT;
}

TER Transactor::payFee ()
{
    STAmount saPaid = mTxn.getTransactionFee ();

    if (!saPaid.isLegalNet ())
        return temBAD_AMOUNT;

    // Only check fee is sufficient when the ledger is open.
    if (isSetBit (mParams, tapOPEN_LEDGER) && saPaid < mFeeDue)
    {
        WriteLog (lsINFO, Transactor) << boost::str (boost::format ("applyTransaction: Insufficient fee paid: %s/%s")
                                      % saPaid.getText ()
                                      % mFeeDue.getText ());

        return telINSUF_FEE_P;
    }

    if (saPaid.isNegative () || !saPaid.isNative ())
        return temBAD_FEE;

    if (!saPaid) return tesSUCCESS;

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back, if the transaction succeeds.
    if (mSourceBalance < saPaid)
    {
        WriteLog (lsINFO, Transactor)
                << boost::str (boost::format ("applyTransaction: Delay: insufficient balance: balance=%s paid=%s")
                               % mSourceBalance.getText ()
                               % saPaid.getText ());

        return terINSUF_FEE_B;
    }

    mSourceBalance -= saPaid;
    mTxnAccount->setFieldAmount (sfBalance, mSourceBalance);

    return tesSUCCESS;
}

TER Transactor::checkSig ()
{
    // Consistency: Check signature
    // Verify the transaction's signing public key is the key authorized for signing.
    if (mSigningPubKey.getAccountID () == mTxnAccountID)
    {
        // Authorized to continue.
        mSigMaster = true;
        if (mTxnAccount->isFlag(lsfDisableMaster))
	    return tefMASTER_DISABLED;
    }
    else if (mHasAuthKey && mSigningPubKey.getAccountID () == mTxnAccount->getFieldAccount160 (sfRegularKey))
    {
        // Authorized to continue.
        nothing ();
    }
    else if (mHasAuthKey)
    {
        WriteLog (lsINFO, Transactor) << "applyTransaction: Delay: Not authorized to use account.";

        return tefBAD_AUTH;
    }
    else
    {
        WriteLog (lsINFO, Transactor) << "applyTransaction: Invalid: Not authorized to use account.";

        return temBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

TER Transactor::checkSeq ()
{
    uint32 t_seq = mTxn.getSequence ();
    uint32 a_seq = mTxnAccount->getFieldU32 (sfSequence);

    WriteLog (lsTRACE, Transactor) << "Aseq=" << a_seq << ", Tseq=" << t_seq;

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            WriteLog (lsINFO, Transactor) << "applyTransaction: future sequence number";

            return terPRE_SEQ;
        }
        else
        {
            uint256 txID = mTxn.getTransactionID ();

            if (mEngine->getLedger ()->hasTransaction (txID))
                return tefALREADY;
        }

        WriteLog (lsWARNING, Transactor) << "applyTransaction: past sequence number";

        return tefPAST_SEQ;
    }

    // Deprecated: Do not use
    if (mTxn.isFieldPresent (sfPreviousTxnID) &&
            (mTxnAccount->getFieldH256 (sfPreviousTxnID) != mTxn.getFieldH256 (sfPreviousTxnID)))
        return tefWRONG_PRIOR;

    if (mTxn.isFieldPresent (sfAccountTxnID) &&
            (mTxnAccount->getFieldH256 (sfAccountTxnID) != mTxn.getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (mTxn.isFieldPresent (sfLastLedgerSequence) &&
            (mEngine->getLedger()->getLedgerSeq() > mTxn.getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    mTxnAccount->setFieldU32 (sfSequence, t_seq + 1);

    if (mTxnAccount->isFieldPresent (sfAccountTxnID))
        mTxnAccount->setFieldH256 (sfAccountTxnID, mTxn.getTransactionID ());

    return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
TER Transactor::preCheck ()
{
    mTxnAccountID   = mTxn.getSourceAccount ().getAccountID ();

    if (!mTxnAccountID)
    {
        WriteLog (lsWARNING, Transactor) << "applyTransaction: bad source id";

        return temBAD_SRC_ACCOUNT;
    }

    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a transaction has at least been properly signed
    // without going to disk.  Each transaction also notes a source account id.  This is used to verify that the signing key is
    // associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    mSigningPubKey  = RippleAddress::createAccountPublic (mTxn.getSigningPubKey ());

    // Consistency: really signed.
    if (!mTxn.isKnownGood ())
    {
        if (mTxn.isKnownBad () || (!isSetBit (mParams, tapNO_CHECK_SIGN) && !mTxn.checkSign (mSigningPubKey)))
        {
            mTxn.setBad ();
            WriteLog (lsWARNING, Transactor) << "applyTransaction: Invalid transaction: bad signature";
            return temINVALID;
        }

        mTxn.setGood ();
    }

    return tesSUCCESS;
}

TER Transactor::apply ()
{
    TER     terResult   = tesSUCCESS;
    terResult = preCheck ();

    if (terResult != tesSUCCESS) return (terResult);

    Ledger::ScopedLockType sl (mEngine->getLedger ()->mLock, __FILE__, __LINE__);

    mTxnAccount = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (mTxnAccountID));
    calculateFee ();

    // Find source account
    // If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probabilistic.

    if (!mTxnAccount)
    {
        if (mustHaveValidAccount ())
        {
            WriteLog (lsTRACE, Transactor) << boost::str (boost::format ("applyTransaction: Delay transaction: source account does not exist: %s") %
                                           mTxn.getSourceAccount ().humanAccountID ());

            return terNO_ACCOUNT;
        }
    }
    else
    {
        mPriorBalance   = mTxnAccount->getFieldAmount (sfBalance);
        mSourceBalance  = mPriorBalance;
        mHasAuthKey     = mTxnAccount->isFieldPresent (sfRegularKey);
    }

    terResult = checkSeq ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = payFee ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = checkSig ();

    if (terResult != tesSUCCESS) return (terResult);

    if (mTxnAccount)
        mEngine->entryModify (mTxnAccount);

    return doApply ();
}

// vim:ts=4
