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
#include <ripple/app/ledger/LedgerFees.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/core/Config.h>
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
TER transact_SetSignerList (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);

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

#if RIPPLE_ENABLE_MULTI_SIGN

    case ttSIGNER_LIST_SET:
        return transact_SetSignerList (txn, params, engine);

#endif // RIPPLE_ENABLE_MULTI_SIGN

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
    mFeeDue = STAmount (scaleFeeLoad (getApp().getFeeTrack(),
        *mEngine->getLedger(), calculateBaseFee (), mParams & tapADMIN));
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

    if (saPaid < zero || !saPaid.native ())
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

TER Transactor::checkSeq ()
{
    std::uint32_t const t_seq = mTxn.getSequence ();
    std::uint32_t const a_seq = mTxnAccount->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            m_journal.trace <<
                "applyTransaction: has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (mEngine->getLedger ()->hasTransaction (mTxn.getTransactionID ()))
            return tefALREADY;

        m_journal.trace << "applyTransaction: has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

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
    TER result = preCheckAccount ();
    if (result != tesSUCCESS)
        return result;

    return preCheckSigningKey ();
}

TER Transactor::preCheckAccount ()
{
    mTxnAccountID = mTxn.getSourceAccount ().getAccountID ();

    if (!mTxnAccountID)
    {
        m_journal.warning << "applyTransaction: bad transaction source id";
        return temBAD_SRC_ACCOUNT;
    }
    return tesSUCCESS;
}

TER Transactor::preCheckSigningKey ()
{
    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a
    // transaction has at least been properly signed without going to disk.
    // Each transaction also notes a source account id. This is used to verify
    // that the signing key is associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    mSigningPubKey =
        RippleAddress::createAccountPublic (mTxn.getSigningPubKey ());

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
    // No point in going any further if the transaction fee is malformed.
    STAmount const saTxnFee = mTxn.getTransactionFee ();

    if (!saTxnFee.native () || saTxnFee.negative () || !isLegalNet (saTxnFee))
        return temBAD_FEE;

    TER terResult = preCheck ();

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
                "applyTransaction: delay: source account does not exist " <<
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

    terResult = checkSign ();

    if (terResult != tesSUCCESS) return (terResult);

    if (mTxnAccount)
        mEngine->view().entryModify (mTxnAccount);

    return doApply ();
}

TER Transactor::checkSign ()
{
#if RIPPLE_ENABLE_MULTI_SIGN
    // If the mSigningPubKey is empty, then we must be multi-signing.
    if (mSigningPubKey.getAccountPublic ().empty ())
        return checkMultiSign ();
#endif

    return checkSingleSign ();
}

TER Transactor::checkSingleSign ()
{
    // Consistency: Check signature
    // Verify the transaction's signing public key is authorized for signing.
    if (mSigningPubKey.getAccountID () == mTxnAccountID)
    {
        // Authorized to continue.
        mSigMaster = true;
        if (mTxnAccount->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;
    }
    else if (mHasAuthKey &&
        (mSigningPubKey.getAccountID () ==
            mTxnAccount->getFieldAccount160 (sfRegularKey)))
    {
        // Authorized to continue.
    }
    else if (mHasAuthKey)
    {
        m_journal.trace <<
            "applyTransaction: Delay: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        m_journal.trace <<
            "applyTransaction: Invalid: Not authorized to use account.";
        return tefBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

namespace TransactorDetail
{

struct GetSignerListResult
{
    TER ter = tefFAILURE;
    std::uint32_t quorum = std::numeric_limits <std::uint32_t>::max ();
    std::vector<SignerEntries::SignerEntry> signerEntries;
};

// We need the SignerList for every SigningFor while multi-signing.
GetSignerListResult
getSignerList (
    Account signingForAcctID, TransactionEngine* engine, beast::Journal journal)
{
    GetSignerListResult ret;

    uint256 const index = getSignerListIndex (signingForAcctID);
    SLE::pointer accountSignersList =
        engine->view ().entryCache (ltSIGNER_LIST, index);

    // If the signer list doesn't exist the account is not multi-signing.
    if (!accountSignersList)
    {
        journal.trace <<
            "applyTransaction: Invalid: Not a multi-signing account.";
        ret.ter = tefNOT_MULTI_SIGNING;
        return ret;
    }
    ret.quorum = accountSignersList->getFieldU32 (sfSignerQuorum);

    SignerEntries::Decoded signersOnAccountDecode =
        SignerEntries::deserialize (*accountSignersList, journal, "ledger");
    ret.ter = signersOnAccountDecode.ter;

    if (signersOnAccountDecode.ter == tesSUCCESS)
    {
        ret.signerEntries = std::move (signersOnAccountDecode.vec);
    }
    return ret;
}

struct CheckSigningAccountsResult
{
    TER ter = tefFAILURE;
    std::uint32_t weightSum = 0;
};

// Verify that every SigningAccount is a valid SignerEntry.  Sum signer weights.
CheckSigningAccountsResult
checkSigningAccounts (
    std::vector<SignerEntries::SignerEntry> signerEntries,
    STArray const& signingAccounts,
    TransactionEngine* engine,
    beast::Journal journal)
{
    CheckSigningAccountsResult ret;

    // Both the signerEntries and signingAccounts are sorted by account.  So
    // matching signerEntries to signingAccounts should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto signerEntriesItr = signerEntries.begin ();
    for (auto const& signingAccount : signingAccounts)
    {
        Account const signingAcctID =
            signingAccount.getFieldAccount (sfAccount).getAccountID ();

        // Attempt to match the SignerEntry with a SigningAccount;
        while (signerEntriesItr->account < signingAcctID)
        {
            if (++signerEntriesItr == signerEntries.end ())
            {
                journal.trace <<
                    "applyTransaction: Invalid SigningAccount.Account.";
                ret.ter = tefBAD_SIGNATURE;
                return ret;
            }
        }
        if (signerEntriesItr->account != signingAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            journal.trace <<
                "applyTransaction: Invalid SigningAccount.Account.";
            ret.ter = tefBAD_SIGNATURE;
            return ret;
        }
        if (signerEntriesItr->weight <= 0)
        {
            // The SigningAccount has a weight of zero and may not sign.
            journal.trace <<
                "applyTransaction: SigningAccount.Account needs weight > 0.";
            ret.ter = tefBAD_SIGNATURE;
            return ret;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        Account const signingAcctIDFromPubKey =
            RippleAddress::createAccountPublic (
                signingAccount.getFieldVL (sfSigningPubKey)).getAccountID ();

        // Verify that the signingAcctID and the signingAcctIDFromPubKey
        // belong together.  Here is are the rules:
        //
        //   1. "Phantom account": an account that is not in the ledger
        //      A. If signingAcctID == signingAcctIDFromPubKey and the
        //         signingAcctID is not in the ledger then we have a phantom
        //         account.
        //      B. Phantom accounts are always allowed as multi-signers.
        //
        //   2. "Master Key"
        //      A. signingAcctID == signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If the signingAcctID in the ledger does not have the
        //         asfDisableMaster flag set, then the signature is allowed.
        //
        //   3. "Regular Key"
        //      A. signingAcctID != signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If signingAcctIDFromPubKey == signingAcctID.RegularKey (from
        //         ledger) then the signature is allowed.
        //
        // No other signatures are allowed.  (January 2015)

        // In any of these cases we need to know whether the account is in
        // the ledger.  Determine that now.
        uint256 const signerAccountIndex = getAccountRootIndex (signingAcctID);

        SLE::pointer signersAccountRoot =
            engine->view ().entryCache (ltACCOUNT_ROOT, signerAccountIndex);

        if (signingAcctIDFromPubKey == signingAcctID)
        {
            // Either Phantom or Master.  Phantom's automatically pass.
            if (signersAccountRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    signersAccountRoot->getFieldU32 (sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    journal.trace <<
                        "applyTransaction: MultiSignature lsfDisableMaster.";
                    ret.ter = tefMASTER_DISABLED;
                    return ret;
                }
            }
        }
        else
        {
            // May be a Regular Key.  Let's find out.
            // Public key must hash to the account's regular key.
            if (!signersAccountRoot)
            {
                journal.trace <<
                    "applyTransaction: Non-phantom signer lacks account root.";
                ret.ter = tefBAD_SIGNATURE;
                return ret;
            }

            if (!signersAccountRoot->isFieldPresent (sfRegularKey))
            {
                journal.trace <<
                    "applyTransaction: Account lacks RegularKey.";
                ret.ter = tefBAD_SIGNATURE;
                return ret;
            }
            if (signingAcctIDFromPubKey !=
                signersAccountRoot->getFieldAccount160 (sfRegularKey))
            {
                journal.trace <<
                    "applyTransaction: Account doesn't match RegularKey.";
                ret.ter = tefBAD_SIGNATURE;
                return ret;
            }
        }
        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += signerEntriesItr->weight;
    }
    ret.weightSum = weightSum;
    ret.ter = tesSUCCESS;
    return ret;
}

} // namespace TransactorDetail

TER Transactor::checkMultiSign ()
{
    // Get mTxnAccountID's SignerList and Quorum.
    using namespace TransactorDetail;
    GetSignerListResult const outer =
        getSignerList (mTxnAccountID, mEngine, m_journal);

    if (outer.ter != tesSUCCESS)
        return outer.ter;

    // Get the actual array of transaction signers.
    STArray const& multiSigners (mTxn.getFieldArray (sfMultiSigners));

    // Walk the accountSigners performing a variety of checks and see if
    // the quorum is met.

    // Both the multiSigners and accountSigners are sorted by account.  So
    // matching multi-signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto signerEntriesItr = outer.signerEntries.begin ();
    for (auto const& signingFor : multiSigners)
    {
        Account const signingForID =
            signingFor.getFieldAccount (sfAccount).getAccountID ();

        STArray const& signingAccounts =
            signingFor.getFieldArray (sfSigningAccounts);

        // There are two possibilities:
        //  o The signers are direct multi-signers for this account.
        //  o The signers are signing for a multi-signer on this account.
        // Handle those two cases separately.
        if (signingForID == mTxnAccountID)
        {
            // The signers are direct multi-signers for this account.  Results
            // from these signers directly effect the quorum.
            CheckSigningAccountsResult const outerSigningAccountsResult =
                checkSigningAccounts (
                    outer.signerEntries, signingAccounts, mEngine, m_journal);

            if (outerSigningAccountsResult.ter != tesSUCCESS)
                return outerSigningAccountsResult.ter;

            weightSum += outerSigningAccountsResult.weightSum;
        }
        else
        {
            // The signers are signing for a multi-signer on this account.
            // Attempt to match the signingForID with a SignerEntry
            while (signerEntriesItr->account < signingForID)
            {
                if (++signerEntriesItr == outer.signerEntries.end ())
                {
                    m_journal.trace <<
                        "applyTransaction: Invalid SigningFor.Account.";
                    return tefBAD_SIGNATURE;
                }
            }
            if (signerEntriesItr->account != signingForID)
            {
                // The signingForID is not in the SignerEntries.
                m_journal.trace <<
                    "applyTransaction: Invalid SigningFor.Account.";
                return tefBAD_SIGNATURE;
            }
            if (signerEntriesItr->weight <= 0)
            {
                // The SigningFor entry needs a weight greater than zero.
                m_journal.trace <<
                    "applyTransaction: SigningFor.Account needs weight > 0.";
                return tefBAD_SIGNATURE;
            }

            // See if the signingForID has a SignerList.
            GetSignerListResult const inner =
                getSignerList (signingForID, mEngine, m_journal);

            if (inner.ter != tesSUCCESS)
                return inner.ter;

            // Results from these signers indirectly effect the quorum.
            CheckSigningAccountsResult const innerSigningAccountsResult =
                checkSigningAccounts (
                    inner.signerEntries, signingAccounts, mEngine, m_journal);

            if (innerSigningAccountsResult.ter != tesSUCCESS)
                return innerSigningAccountsResult.ter;

            // There's a policy question here.  If the SigningAccounts are
            // all valid but fail to reach this signingFor's Quorum do we:
            //
            //  1. Say the signature is valid but contributes 0 toward the
            //     quorum?
            //
            //  2. Say that any SigningFor that doesn't meet quorum is an
            //     invalid signature and fails?
            //
            // The choice is not obvious to me.  I'm picking 2 for now, since
            // it's more restrictive.  We can switch to policy 1 later without
            // causing transactions that would have worked before to fail.
            // -- January 2015
            if (innerSigningAccountsResult.weightSum < inner.quorum)
            {
                m_journal.trace <<
                    "applyTransaction: Level-2 SigningFor did not make quorum.";
                return tefBAD_QUORUM;
            }
            // This SigningFor met quorum.  Add its weight.
            weightSum += signerEntriesItr->weight;
        }
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < outer.quorum)
    {
        m_journal.trace <<
            "applyTransaction: MultiSignature failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

}
