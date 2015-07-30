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
#include <ripple/app/main/Application.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/types.h>

namespace ripple {

Transactor::Transactor(
    ApplyContext& ctx)
    : mTxn (ctx.tx)
    , ctx_ (ctx)
    , j_ (ctx.journal)
    , mHasAuthKey (false)
    , mSigMaster (false)
{
}

void Transactor::calculateFee ()
{
    mFeeDue = STAmount (getApp().getFeeTrack().scaleFeeLoad(
        calculateBaseFee(), view().fees().base,
            view().fees().units, view().flags() & tapADMIN));
}

std::uint64_t Transactor::calculateBaseFee ()
{
    // Returns the fee in fee units
    return ctx_.config.TRANSACTION_FEE_BASE;
}

TER Transactor::payFee ()
{
    STAmount saPaid = mTxn.getTransactionFee ();

    if (!isLegalNet (saPaid))
        return temBAD_AMOUNT;

    // Only check fee is sufficient when the ledger is open.
    if (view().open() && saPaid < mFeeDue)
    {
        j_.trace << "Insufficient fee paid: " <<
            saPaid.getText () << "/" << mFeeDue.getText ();

        return telINSUF_FEE_P;
    }

    if (saPaid < zero || !saPaid.native ())
        return temBAD_FEE;

    if (!saPaid)
        return tesSUCCESS;

    auto const sle = view().peek(
        keylet::account(account_));

    if (mSourceBalance < saPaid)
    {
        j_.trace << "Insufficient balance:" <<
            " balance=" << mSourceBalance.getText () <<
            " paid=" << saPaid.getText ();

        if ((mSourceBalance > zero) && ! view().open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back, if the transaction succeeds.

    mSourceBalance -= saPaid;
    sle->setFieldAmount (sfBalance, mSourceBalance);

    // VFALCO Should we call view().rawDestroyXRP() here as well?

    return tesSUCCESS;
}

TER Transactor::checkSeq ()
{
    auto const sle = view().peek(
        keylet::account(account_));

    std::uint32_t const t_seq = mTxn.getSequence ();
    std::uint32_t const a_seq = sle->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            j_.trace <<
                "applyTransaction: has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (view().txExists(mTxn.getTransactionID ()))
            return tefALREADY;

        j_.trace << "applyTransaction: has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

    if (mTxn.isFieldPresent (sfAccountTxnID) &&
            (sle->getFieldH256 (sfAccountTxnID) != mTxn.getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (mTxn.isFieldPresent (sfLastLedgerSequence) &&
            (view().seq() > mTxn.getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    sle->setFieldU32 (sfSequence, t_seq + 1);

    if (sle->isFieldPresent (sfAccountTxnID))
        sle->setFieldH256 (sfAccountTxnID, mTxn.getTransactionID ());

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
    account_ = mTxn.getAccountID(sfAccount);

    if (!account_)
    {
        j_.warning << "applyTransaction: bad transaction source id";
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
            (!(view().flags() & tapNO_CHECK_SIGN) && !mTxn.checkSign(
                (
#if RIPPLE_ENABLE_MULTI_SIGN
                    true
#else
                    view().flags() & tapENABLE_TESTING
#endif
                ))))
        {
            mTxn.setBad ();
            j_.debug << "apply: Invalid transaction (bad signature)";
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
    auto const sle = view().peek (keylet::account(account_));

    calculateFee ();

    // If are only forwarding, due to resource limitations, we might verifying
    // only some transactions, this would be probabilistic.
    if (!sle)
    {
        if (mustHaveValidAccount ())
        {
            j_.trace <<
                "applyTransaction: delay: source account does not exist " <<
                toBase58(mTxn.getAccountID(sfAccount));
            return terNO_ACCOUNT;
        }
    }
    else
    {
        mPriorBalance   = sle->getFieldAmount (sfBalance);
        mSourceBalance  = mPriorBalance;
        mHasAuthKey     = sle->isFieldPresent (sfRegularKey);
    }

    terResult = checkSeq ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = payFee ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = checkSign ();

    if (terResult != tesSUCCESS) return (terResult);

    if (sle)
        view().update (sle);

    return doApply ();
}

TER Transactor::checkSign ()
{
#if RIPPLE_ENABLE_MULTI_SIGN
#else
    if(view().flags() & tapENABLE_TESTING)
#endif
    {
        // If the mSigningPubKey is empty, then we must be multi-signing.
        if (mSigningPubKey.getAccountPublic ().empty ())
            return checkMultiSign ();
    }

    return checkSingleSign ();
}

TER Transactor::checkSingleSign ()
{
    // VFALCO NOTE This is needlessly calculating the
    //             AccountID multiple times.

    // VFALCO What if sle is nullptr?
    auto const sle = view().peek(
        keylet::account(account_));

    // Consistency: Check signature
    // Verify the transaction's signing public key is authorized for signing.
    if (calcAccountID(mSigningPubKey) == account_)
    {
        // Authorized to continue.
        mSigMaster = true;
        if (sle->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;
    }
    else if (mHasAuthKey &&
        (calcAccountID(mSigningPubKey) ==
            sle->getAccountID (sfRegularKey)))
    {
        // Authorized to continue.
    }
    else if (mHasAuthKey)
    {
        j_.trace <<
            "applyTransaction: Delay: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        j_.trace <<
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
static
GetSignerListResult
getSignerList (AccountID signingForAcctID,
    ReadView const& view, beast::Journal journal)
{
    GetSignerListResult ret;

    auto const k = keylet::signers(signingForAcctID);
    auto const accountSignersList =
        view.read (k);

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
    ApplyContext& ctx,
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
        auto const signingAcctID =
            signingAccount.getAccountID(sfAccount);

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
        AccountID const signingAcctIDFromPubKey =
            calcAccountID(RippleAddress::createAccountPublic (
                signingAccount.getFieldVL (sfSigningPubKey)));

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
        auto signersAccountRoot =
            ctx.view().read (keylet::account(signingAcctID));

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
                signersAccountRoot->getAccountID (sfRegularKey))
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
    // Get account_'s SignerList and Quorum.
    using namespace TransactorDetail;
    GetSignerListResult const outer =
        getSignerList (account_, view(), j_);

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
        auto const signingForID =
            signingFor.getAccountID(sfAccount);

        auto const& signingAccounts =
            signingFor.getFieldArray (sfSigningAccounts);

        // There are two possibilities:
        //  o The signers are direct multi-signers for this account.
        //  o The signers are signing for a multi-signer on this account.
        // Handle those two cases separately.
        if (signingForID == account_)
        {
            // The signers are direct multi-signers for this account.  Results
            // from these signers directly effect the quorum.
            CheckSigningAccountsResult const outerSigningAccountsResult =
                checkSigningAccounts (
                    outer.signerEntries, signingAccounts, ctx_, j_);

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
                    j_.trace <<
                        "applyTransaction: Invalid SigningFor.Account.";
                    return tefBAD_SIGNATURE;
                }
            }
            if (signerEntriesItr->account != signingForID)
            {
                // The signingForID is not in the SignerEntries.
                j_.trace <<
                    "applyTransaction: Invalid SigningFor.Account.";
                return tefBAD_SIGNATURE;
            }
            if (signerEntriesItr->weight <= 0)
            {
                // The SigningFor entry needs a weight greater than zero.
                j_.trace <<
                    "applyTransaction: SigningFor.Account needs weight > 0.";
                return tefBAD_SIGNATURE;
            }

            // See if the signingForID has a SignerList.
            GetSignerListResult const inner =
                getSignerList (signingForID, view(), j_);

            if (inner.ter != tesSUCCESS)
                return inner.ter;

            // Results from these signers indirectly effect the quorum.
            CheckSigningAccountsResult const innerSigningAccountsResult =
                checkSigningAccounts (
                    inner.signerEntries, signingAccounts, ctx_, j_);

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
                j_.trace <<
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
        j_.trace <<
            "applyTransaction: MultiSignature failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static
inline
void
log (std::pair<
    TER, bool> const& result,
        beast::Journal j)
{
#if 0
    JLOG(j.error) <<
        "apply: { " << transToken(result.first) <<
        ", " << (result.second ? "true" : "false") << " }";
#endif
}

std::pair<TER, bool>
Transactor::operator()()
{
    JLOG(j_.trace) <<
        "applyTransaction>";

    uint256 const& txID = mTxn.getTransactionID ();

    if (!txID)
    {
        JLOG(j_.warning) <<
            "applyTransaction: transaction id may not be zero";
        auto const result =
            std::make_pair(temINVALID_FLAG, false);
        log(result, j_);
        return result;
    }

#ifdef BEAST_DEBUG
    {
        Serializer ser;
        mTxn.add (ser);
        SerialIter sit(ser.slice());
        STTx s2 (sit);

        if (! s2.isEquivalent(mTxn))
        {
            JLOG(j_.fatal) <<
                "Transaction serdes mismatch";
            JLOG(j_.info) << to_string(mTxn.getJson (0));
            JLOG(j_.fatal) << s2.getJson (0);
            assert (false);
        }
    }
#endif

    TER terResult = apply();

    if (terResult == temUNKNOWN)
    {
        JLOG(j_.warning) <<
            "applyTransaction: Invalid transaction: unknown transaction type";
        auto const result =
            std::make_pair(temUNKNOWN, false);
        log(result, j_);
        return result;
    }

    if (j_.debug)
    {
        std::string strToken;
        std::string strHuman;

        transResultInfo (terResult, strToken, strHuman);

        j_.debug <<
            "applyTransaction: terResult=" << strToken <<
            " : " << terResult <<
            " : " << strHuman;
    }

    bool didApply = isTesSuccess (terResult);
    auto fee = mTxn.getTransactionFee ();

    if (isTecClaim (terResult) && !(view().flags() & tapRETRY))
    {
        // only claim the transaction fee
        JLOG(j_.debug) <<
            "Reprocessing tx " << txID << " to only claim fee";
        
        ctx_.discard();

        auto const txnAcct = view().peek(
            keylet::account(mTxn.getAccountID(sfAccount)));

        if (txnAcct)
        {
            std::uint32_t t_seq = mTxn.getSequence ();
            std::uint32_t a_seq = txnAcct->getFieldU32 (sfSequence);

            if (a_seq < t_seq)
                terResult = terPRE_SEQ;
            else if (a_seq > t_seq)
                terResult = tefPAST_SEQ;
            else
            {
                STAmount balance    = txnAcct->getFieldAmount (sfBalance);

                // We retry/reject the transaction if the account
                // balance is zero or we're applying against an open
                // ledger and the balance is less than the fee
                if ((balance == zero) ||
                    (view().open() && (balance < fee)))
                {
                    // Account has no funds or ledger is open
                    terResult = terINSUF_FEE_B;
                }
                else
                {
                    if (fee > balance)
                        fee = balance;
                    txnAcct->setFieldAmount (sfBalance, balance - fee);
                    txnAcct->setFieldU32 (sfSequence, t_seq + 1);
                    view().update (txnAcct);
                    didApply = true;
                }
            }
        }
        else
        {
            terResult = terNO_ACCOUNT;
        }
    }
    else if (!didApply)
    {
        JLOG(j_.debug) << "Not applying transaction " << txID;
    }

    if (didApply)
    {
        // Transaction succeeded fully or (retries are
        // not allowed and the transaction could claim a fee)

        if(view().closed())
        {
            // VFALCO Fix this nonsense with Amount
            // Charge whatever fee they specified. We break the
            // encapsulation of STAmount here and use "special
            // knowledge" - namely that a native amount is
            // stored fully in the mantissa:

            // The transactor guarantees these will never trigger
            if (!fee.native () || fee.negative ())
            {
                // VFALCO Log to journal here
                // JLOG(journal.fatal) << "invalid fee";
                throw std::logic_error(
                    "amount is negative!");
            }

            if (fee != zero)
                ctx_.destroyXRP (fee.mantissa ());
        }

        ctx_.apply(terResult);
        // VFALCO NOTE since we called apply(), it is not
        //             okay to look at view() past this point.
    }

    auto const result =
        std::make_pair(terResult, didApply);
    log(result, j_);
    return result;
}

}
