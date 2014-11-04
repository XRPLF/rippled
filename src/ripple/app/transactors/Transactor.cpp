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

#include <ripple/app/transactors/Transactor.h>
#include <ripple/app/transactors/Impl/SignerEntries.h>
#include <ripple/core/Config.h>

namespace ripple {

TER transact_Payment (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetAccount (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetRegularKey (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetTrust (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateOffer (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelOffer (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_AddWallet (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_Change (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateTicket (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelTicket (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetSignerList (SerializedTransaction const& txn, TransactionEngineParams params, TransactionEngine* engine);

TER
Transactor::transact (
    SerializedTransaction const& txn,
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

    case ttWALLET_ADD:
        return transact_AddWallet (txn, params, engine);

    case ttAMENDMENT:
    case ttFEE:
        return transact_Change (txn, params, engine);

    case ttTICKET_CREATE:
        return transact_CreateTicket (txn, params, engine);

    case ttTICKET_CANCEL:
        return transact_CancelTicket (txn, params, engine);

    case ttSIGNER_LIST_SET:
        return transact_SetSignerList (txn, params, engine);

    default:
        return temUNKNOWN;
    }
}

Transactor::Transactor (
    SerializedTransaction const& txn,
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
    }
    else if (mHasAuthKey)
    {
        m_journal.trace << "applyTransaction: Delay: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        m_journal.trace << "applyTransaction: Invalid: Not authorized to use account.";

        return temBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

TER Transactor::checkSeq ()
{
    std::uint32_t t_seq = mTxn.getSequence ();
    std::uint32_t a_seq = mTxnAccount->getFieldU32 (sfSequence);

    m_journal.trace << "Aseq=" << a_seq << ", Tseq=" << t_seq;

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            m_journal.trace << "apply: transaction has future sequence number";

            return terPRE_SEQ;
        }
        else
        {
            if (mEngine->getLedger ()->hasTransaction (mTxn.getTransactionID ()))
                return tefALREADY;
        }

        m_journal.warning << "apply: transaction has past sequence number";

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
        if (mTxn.isKnownBad ())
        {
            mTxn.setBad ();
            m_journal.warning << "apply: Invalid transaction (bad signature)";
            return temINVALID;
        }

        TER const signingTER = checkSign();
        if (signingTER != tesSUCCESS)
            return signingTER;

        mTxn.setGood ();
    }

    return tesSUCCESS;
}

TER Transactor::apply ()
{
    TER terResult (preCheck ());

    if (terResult != tesSUCCESS)
        return (terResult);

    mTxnAccount = mEngine->entryCache (ltACCOUNT_ROOT,
        Ledger::getAccountRootIndex (mTxnAccountID));
    calculateFee ();

    // Find source account
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
        mEngine->entryModify (mTxnAccount);

    return doApply ();
}

TER Transactor::checkSign ()
{
    // Only check signature if we need to
    if (mParams & tapNO_CHECK_SIGN)
        return tesSUCCESS;

    TER signingTER = temINVALID;

    // If the signingPubKey is empty then we must be multi-signing.
    if (mSigningPubKey.getAccountPublic ().empty ())
        signingTER = checkMultiSign ();
    else
        signingTER = checkSingleSign ();

    if (signingTER != tesSUCCESS)
        mTxn.setBad ();

    return signingTER;
}

TER Transactor::checkSingleSign ()
{
    if (mTxn.checkSign (mSigningPubKey))
        return tesSUCCESS;

    m_journal.warning << "apply: Invalid transaction (bad signature)";
    return temINVALID;
}

TER Transactor::checkMultiSign ()
{
    // Make sure the SignerEntries are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SccountID
    if (!mTxn.isFieldPresent (sfSigningAccounts))
    {
        m_journal.trace <<
            "applyTransaction: Invalid: Not authorized to use account.";
        return temBAD_AUTH_MASTER;
    }
    STArray const& txnSigners (mTxn.getFieldArray (sfSigningAccounts));

    // The TxnAccount must be multi-signed for this to work.
    // See if there's an ltSIGNER_LIST for this account.
    uint256 const index = Ledger::getSignerListIndex (mTxnAccountID);

    // Get a vector of the account's signers.
    std::uint32_t quorum = std::numeric_limits <std::uint32_t>::max ();
    SignerEntries::SignerEntryVec accountSigners;
    {
        SLE::pointer accountSignersList =
            mEngine->view ().entryCache (ltSIGNER_LIST, index);

        // If the signer list doesn't exist the account is not multi-signing.
        if (!accountSignersList)
        {
            m_journal.trace <<
                "applyTransaction: Invalid: Not authorized to use account.";
            return temBAD_AUTH_MASTER;
        }
        quorum = accountSignersList->getFieldU32 (sfSignerQuorum);

        SignerEntries::SignerEntriesDecode signersOnAccountDecode =
            SignerEntries::deserializeSignerEntries (
                *accountSignersList, m_journal, "ledger");

        if (signersOnAccountDecode.ter != tesSUCCESS)
            return signersOnAccountDecode.ter;

        accountSigners = std::move (signersOnAccountDecode.vec);
    }

    // The transaction hash is needed repeatedly inside the loop.  Compute
    // it once outside the loop.
    uint256 const trans_hash = mTxn.getSigningHash ();

    // Walk the SignerEntries performing a variety of checks until the
    // quorum is met (or not).

    // Both the txnSigners and accountSigners are sorted by
    // account.  So Matching transaction signers to account signers should be
    // a simple linear walk.
    std::uint32_t weightSum = 0;
    auto accountSignersItr = accountSigners.begin ();
    for (auto const& txnSigner : txnSigners)
    {
        // There should be exactly three fields in each SigningAccount
        if ((txnSigner.getCount () != 3) ||
            (!txnSigner.isFieldPresent (sfAccount)) ||
            (!txnSigner.isFieldPresent (sfPublicKey)) ||
            (!txnSigner.isFieldPresent (sfMultiSignature)))
        {
            m_journal.trace <<
                "applyTransaction: Malformed SigningAccount.";
            return temMALFORMED;
        }

        // All required fields are present.  See if this is a valid signer.
        Account const txnSignerID =
            txnSigner.getFieldAccount (sfAccount).getAccountID ();

        // Attempt to match the txnSignerID with an AccountSigner
        while (accountSignersItr->account < txnSignerID)
        {
            if (++accountSignersItr == accountSigners.end ())
            {
                m_journal.trace <<
                    "applyTransaction: Invalid SignerEntry.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (accountSignersItr->account != txnSignerID)
        {
            // The txnSignerID is not in the SignerEntries.
            m_journal.trace <<
                "applyTransaction: Invalid SignerEntry.Account.";
            return tefBAD_SIGNATURE;
        }

        // Verify that the AccountID and the PublicKey belong together.
        // Here are the instructions from David for validating that an
        // AccountID and PublicKey belong together (November 2014):
        //
        // "Check the ledger for the account. If the public key hashes to the
        // account, then the master key must not be disabled. If it does not,
        // then the public key must hash to the account's RegularKey."
        uint256 const signerAccountIndex =
            Ledger::getAccountRootIndex (txnSignerID);
        SLE::pointer signersAccountRoot =
            mEngine->view ().entryCache (ltACCOUNT_ROOT, signerAccountIndex);

        if (!signersAccountRoot)
        {
            // SigningAccount is not present in the ledger.
            m_journal.trace <<
                "applyTransaction: SignerEntry.Account is missing from ledger.";
            return tefBAD_SIGNATURE;
        }

        RippleAddress const pubKey = RippleAddress::createAccountPublic (
            txnSigner.getFieldVL (sfPublicKey));
        Account const accountFromPublicKey = pubKey.getAccountID ();

        std::uint32_t const signerAccountFlags =
            signersAccountRoot->getFieldU32 (sfFlags);

        if (signerAccountFlags & lsfDisableMaster)
        {
            // Public key must hash to the account's regular key.
            if (!signersAccountRoot->isFieldPresent (sfRegularKey))
            {
                m_journal.trace <<
                    "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (accountFromPublicKey !=
                signersAccountRoot->getFieldAccount160 (sfRegularKey))
            {
                m_journal.trace <<
                    "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        else
        {
            // Public key must hash to AccountID
            if (accountFromPublicKey != txnSignerID)
            {
                // Public key does not match account.  Fail.
                m_journal.trace <<
                    "applyTransaction:: Public key doesn't match account ID.";
                return tefBAD_SIGNATURE;
            }
        }
        // We've just finished showing that the AccountID and PublicKey
        // belong together.

        // We've probably got a legitimate signer.  Check the signature.
        Blob const signature =
            txnSigner.getFieldVL (sfMultiSignature);

        bool validSig = false;
        try
        {
            validSig = pubKey.accountPublicVerify (
                trans_hash, signature, ECDSA::not_strict);
        }
        catch (...)
        {
            // We assume any problem lies with the signature.  That's better
            // than returning "internal error".
        }
        if (!validSig)
        {
            m_journal.trace <<
                "applyTransaction: Invalid SignerEntry.MultiSignature.";
            return tefBAD_SIGNATURE;
        }

        // Signature matches.  Increase the weightSum and continue.
        weightSum += accountSignersItr->weight;

        if (weightSum >= quorum)
            break;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < quorum)
    {
        m_journal.trace <<
            "applyTransaction: MultiSignature failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

}
