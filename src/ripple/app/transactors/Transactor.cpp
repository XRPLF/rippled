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
#include <ripple/core/Config.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/legacy/0.27/Emulate027.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

TER transact_Payment (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetAccount (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetRegularKey (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetTrust (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateOffer (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelOffer (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_Change (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateTicket (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelTicket (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);

TER
Transactor::transact (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    switch (txn.getTxnType ())
    {
    case ttPAYMENT:
        return transact_Payment (txn, params, engine);

    case ttACCOUNT_SET:
        return transact_SetAccount (txn, params, engine);

    case ttREGULAR_KEY_SET:
        return transact_SetRegularKey (txn, params, engine);

    case ttTRUST_SET:
        return transact_SetTrust (txn, params, engine);

    case ttOFFER_CREATE:
        return transact_CreateOffer (txn, params, engine);

    case ttOFFER_CANCEL:
        return transact_CancelOffer (txn, params, engine);

    case ttAMENDMENT:
    case ttFEE:
        return transact_Change (txn, params, engine);

    case ttTICKET_CREATE:
        return transact_CreateTicket (txn, params, engine);

    case ttTICKET_CANCEL:
        return transact_CancelTicket (txn, params, engine);

    default:
        return temUNKNOWN;
    }
}

Transactor::Transactor (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine,
    beast::Journal journal)
    : mTxn (txn)
    , mEngine (engine)
    , mParams (params)
    , mHasAuthKey (false)
    , mSigMaster (false)
    , m_journal (journal)
{
}

void Transactor::calculateFee ()
{
    mFeeDue = STAmount (mEngine->getLedger ()->scaleFeeLoad (
        calculateBaseFee (), mParams & tapADMIN));
}

std::uint64_t Transactor::calculateBaseFee ()
{
    // Returns the fee in fee units
    return getConfig ().TRANSACTION_FEE_BASE;
}

TER Transactor::payFee ()
{
    STAmount saPaid = mTxn.getTransactionFee ();

    if (!isLegalNet (saPaid))
        return temBAD_AMOUNT;

    // Only check fee is sufficient when the ledger is open.
    if ((mParams & tapOPEN_LEDGER) && saPaid < mFeeDue)
    {
        m_journal.trace << "Insufficient fee paid: " <<
            saPaid.getText () << "/" << mFeeDue.getText ();

        return telINSUF_FEE_P;
    }

    if (saPaid < zero || !saPaid.isNative ())
        return temBAD_FEE;

    if (!saPaid)
        return tesSUCCESS;

    if (mSourceBalance < saPaid)
    {
        m_journal.trace << "Insufficient balance:" <<
            " balance=" << mSourceBalance.getText () <<
            " paid=" << saPaid.getText ();

        if ((mSourceBalance > zero) && (!(mParams & tapOPEN_LEDGER)))
        {
            // Closed ledger, non-zero balance, less than fee
            mSourceBalance.clear ();
            mTxnAccount->setFieldAmount (sfBalance, mSourceBalance);
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back, if the transaction succeeds.

    mSourceBalance -= saPaid;
    mTxnAccount->setFieldAmount (sfBalance, mSourceBalance);

    return tesSUCCESS;
}

TER Transactor::checkSig ()
{
    // Consistency: Check signature and verify the transaction's signing public
    // key is the key authorized for signing.

    auto const signing_account = mSigningPubKey.getAccountID ();

    if (signing_account == mTxnAccountID)
    {
        if (mTxnAccount->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;

        mSigMaster = true;
        return tesSUCCESS;
    }

    if (!mHasAuthKey)
    {
        m_journal.trace << "Invalid: Not authorized to use account.";
        return temBAD_AUTH_MASTER;
    }

    if (signing_account == mTxnAccount->getFieldAccount160 (sfRegularKey))
        return tesSUCCESS;

    m_journal.trace << "Delay: Not authorized to use account.";
    return tefBAD_AUTH;
}

TER Transactor::checkSeq ()
{
    std::uint32_t const t_seq = mTxn.getSequence ();
    std::uint32_t const a_seq = mTxnAccount->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            m_journal.trace << "Transaction has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (mEngine->getLedger ()->hasTransaction (mTxn.getTransactionID ()))
            return tefALREADY;

        m_journal.trace << "Transaction has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

    if (ripple::legacy::emulate027 (mEngine->getLedger()) &&
            mTxn.isFieldPresent (sfPreviousTxnID) &&
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
    mTxnAccountID = mTxn.getSourceAccount ().getAccountID ();

    if (!mTxnAccountID)
    {
        m_journal.warning << "apply: bad transaction source id";
        return temBAD_SRC_ACCOUNT;
    }

    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a
    // transaction has at least been properly signed without going to disk.
    // Each transaction also notes a source account id. This is used to verify
    // that the signing key is associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    mSigningPubKey = RippleAddress::createAccountPublic (mTxn.getSigningPubKey ());

    // Consistency: really signed.
    if (!mTxn.isKnownGood ())
    {
        if (mTxn.isKnownBad () ||
            (!(mParams & tapNO_CHECK_SIGN) && !mTxn.checkSign()))
        {
            mTxn.setBad ();
            m_journal.debug << "apply: Invalid transaction (bad signature)";
            return temINVALID;
        }

        mTxn.setGood ();
    }

    return tesSUCCESS;
}

TER Transactor::apply ()
{
    TER terResult (preCheck ());

    if (terResult != tesSUCCESS)
        return terResult;

    // Find source account
    mTxnAccount = mEngine->view().entryCache (ltACCOUNT_ROOT,
        getAccountRootIndex (mTxnAccountID));

    calculateFee ();

    // If are only forwarding, due to resource limitations, we might verifying
    // only some transactions, this would be probabilistic.
    if (!mTxnAccount)
    {
        if (mustHaveValidAccount ())
        {
            m_journal.trace <<
                "apply: delay transaction: source account does not exist " <<
                mTxn.getSourceAccount ().humanAccountID ();
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
        mEngine->view().entryModify (mTxnAccount);

    return doApply ();
}

}
