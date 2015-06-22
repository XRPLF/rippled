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

TER
Transactor::preflight (PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    auto const id = tx.getAccountID(sfAccount);
    if (id == zero)
    {
        JLOG(j.warning) << "Transactor::preflight: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a
    // transaction has at least been properly signed without going to disk.
    // Each transaction also notes a source account id. This is used to verify
    // that the signing key is associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    auto const pk =
        RippleAddress::createAccountPublic(
            tx.getSigningPubKey());

    if(! ctx.verify(tx,
        [&, ctx] (STTx const& tx)
        {
            return (ctx.flags & tapNO_CHECK_SIGN) ||
                tx.checkSign(
#if RIPPLE_ENABLE_MULTI_SIGN
                    true
#else
                    ctx.flags & tapENABLE_TESTING
#endif
                );
        }))
    {
        JLOG(j.debug) << "apply: Invalid transaction (bad signature)";
        return temINVALID;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

Transactor::Transactor(
    ApplyContext& ctx)
    : ctx_ (ctx)
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
    STAmount saPaid = tx().getTransactionFee ();

    if (!isLegalNet (saPaid))
        return temBAD_AMOUNT;

    // Only check fee is sufficient when the ledger is open.
    if (view().open() && saPaid < mFeeDue)
    {
        JLOG(j_.trace) << "Insufficient fee paid: " <<
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
        JLOG(j_.trace) << "Insufficient balance:" <<
            " balance=" << mSourceBalance.getText () <<
            " paid=" << saPaid.getText ();

        if ((mSourceBalance > zero) && ! view().open())
        {
            // Closed ledger, non-zero balance, less than fee
            mSourceBalance.clear ();
            sle->setFieldAmount (sfBalance, mSourceBalance);
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

    std::uint32_t const t_seq = tx().getSequence ();
    std::uint32_t const a_seq = sle->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            JLOG(j_.trace) <<
                "applyTransaction: has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (view().txExists(tx().getTransactionID ()))
            return tefALREADY;

        JLOG(j_.trace) << "applyTransaction: has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

    if (tx().isFieldPresent (sfAccountTxnID) &&
            (sle->getFieldH256 (sfAccountTxnID) != tx().getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (tx().isFieldPresent (sfLastLedgerSequence) &&
            (view().seq() > tx().getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    sle->setFieldU32 (sfSequence, t_seq + 1);

    if (sle->isFieldPresent (sfAccountTxnID))
        sle->setFieldH256 (sfAccountTxnID, tx().getTransactionID ());

    return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
void Transactor::preCompute ()
{
    account_ = tx().getAccountID(sfAccount);
    assert(account_ != zero);
    mSigningPubKey =
        RippleAddress::createAccountPublic(
            tx().getSigningPubKey());
}

TER Transactor::apply ()
{
    // No point in going any further if the transaction fee is malformed.
    STAmount const saTxnFee = tx().getTransactionFee ();

    if (!saTxnFee.native () || saTxnFee.negative () || !isLegalNet (saTxnFee))
        return temBAD_FEE;

    preCompute();

    // Find source account
    auto const sle = view().peek (keylet::account(account_));

    calculateFee ();

    // If are only forwarding, due to resource limitations, we might verifying
    // only some transactions, this would be probabilistic.
    if (!sle)
    {
        if (mustHaveValidAccount ())
        {
            JLOG(j_.trace) <<
                "applyTransaction: delay: source account does not exist " <<
                toBase58(tx().getAccountID(sfAccount));
            return terNO_ACCOUNT;
        }
    }
    else
    {
        mPriorBalance   = sle->getFieldAmount (sfBalance);
        mSourceBalance  = mPriorBalance;
        mHasAuthKey     = sle->isFieldPresent (sfRegularKey);
    }

    auto terResult = checkSeq ();

    if (terResult != tesSUCCESS) return terResult;

    terResult = payFee ();

    if (terResult != tesSUCCESS) return terResult;

    terResult = checkSign ();

    if (terResult != tesSUCCESS) return terResult;

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
    auto const sle = view().peek(keylet::account(account_));
    if (! sle)
        return tefFAILURE;  // We really expected to find the account.

    // Consistency: Check signature
    // Verify the transaction's signing public key is authorized for signing.
    AccountID const idFromPubKey = calcAccountID(mSigningPubKey);
    if (idFromPubKey == account_)
    {
        // Authorized to continue.
        mSigMaster = true;
        if (sle->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;
    }
    else if (mHasAuthKey && (idFromPubKey == sle->getAccountID (sfRegularKey)))
    {
        // Authorized to continue.
    }
    else if (mHasAuthKey)
    {
        JLOG(j_.trace) <<
            "applyTransaction: Delay: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        JLOG(j_.trace) <<
            "applyTransaction: Invalid: Not authorized to use account.";
        return tefBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

TER Transactor::checkMultiSign ()
{
    // Get mTxnAccountID's SignerList and Quorum.
    std::shared_ptr<STLedgerEntry const> sleAccountSigners =
        view().read (keylet::signers(account_));
    // If the signer list doesn't exist the account is not multi-signing.
    if (!sleAccountSigners)
    {
        JLOG(j_.trace) <<
            "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // We have plans to support multiple SignerLists in the future.  The
    // presence and defaulted value of the SignerListID field will enable that.
    assert (sleAccountSigners->isFieldPresent (sfSignerListID));
    assert (sleAccountSigners->getFieldU32 (sfSignerListID) == 0);

    auto accountSigners =
        SignerEntries::deserialize (*sleAccountSigners, j_, "ledger");
    if (accountSigners.second != tesSUCCESS)
        return accountSigners.second;

    // Get the array of transaction signers.
    STArray const& txSigners (tx().getFieldArray (sfSigners));

    // Walk the accountSigners performing a variety of checks and see if
    // the quorum is met.

    // Both the multiSigners and accountSigners are sorted by account.  So
    // matching multi-signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto iter = accountSigners.first.begin ();
    for (auto const& txSigner : txSigners)
    {
        AccountID const txSignerAcctID = txSigner.getAccountID (sfAccount);

        // Attempt to match the SignerEntry with a Signer;
        while (iter->account < txSignerAcctID)
        {
            if (++iter == accountSigners.first.end ())
            {
                JLOG(j_.trace) <<
                    "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            JLOG(j_.trace) <<
                "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        AccountID const signingAcctIDFromPubKey =
            calcAccountID(RippleAddress::createAccountPublic (
                txSigner.getFieldVL (sfSigningPubKey)));

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
        std::shared_ptr<STLedgerEntry const> sleTxSignerRoot =
            view().read (keylet::account(txSignerAcctID));

        if (signingAcctIDFromPubKey == txSignerAcctID)
        {
            // Either Phantom or Master.  Phantoms automatically pass.
            if (sleTxSignerRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    sleTxSignerRoot->getFieldU32 (sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    JLOG(j_.trace) <<
                        "applyTransaction: Signer:Account lsfDisableMaster.";
                    return tefMASTER_DISABLED;
                }
            }
        }
        else
        {
            // May be a Regular Key.  Let's find out.
            // Public key must hash to the account's regular key.
            if (!sleTxSignerRoot)
            {
                JLOG(j_.trace) <<
                    "applyTransaction: Non-phantom signer lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent (sfRegularKey))
            {
                JLOG(j_.trace) <<
                    "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey !=
                sleTxSignerRoot->getAccountID (sfRegularKey))
            {
                JLOG(j_.trace) <<
                    "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += iter->weight;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < sleAccountSigners->getFieldU32 (sfSignerQuorum))
    {
        JLOG(j_.trace) <<
            "applyTransaction: Signers failed to meet quorum.";
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

    uint256 const& txID = tx().getTransactionID ();

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
        tx().add (ser);
        SerialIter sit(ser.slice());
        STTx s2 (sit);

        if (! s2.isEquivalent(tx()))
        {
            JLOG(j_.fatal) <<
                "Transaction serdes mismatch";
            JLOG(j_.info) << to_string(tx().getJson (0));
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

    if (isTecClaim (terResult) && !(view().flags() & tapRETRY))
    {
        // only claim the transaction fee
        JLOG(j_.debug) <<
            "Reprocessing tx " << txID << " to only claim fee";

        ctx_.discard();

        auto const txnAcct = view().peek(
            keylet::account(tx().getAccountID(sfAccount)));

        if (txnAcct)
        {
            std::uint32_t t_seq = tx().getSequence ();
            std::uint32_t a_seq = txnAcct->getFieldU32 (sfSequence);

            if (a_seq < t_seq)
                terResult = terPRE_SEQ;
            else if (a_seq > t_seq)
                terResult = tefPAST_SEQ;
            else
            {
                STAmount fee        = tx().getTransactionFee ();
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
            auto const fee = tx().getTransactionFee ();

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
