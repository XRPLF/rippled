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
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/types.h>

namespace ripple {

/** Performs early sanity checks on the account and fee fields */
TER
preflight1 (PreflightContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);
    if (id == zero)
    {
        JLOG(ctx.j.warning) << "preflight1: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount (sfFee);
    if (!fee.native () || fee.negative () || !isLegalAmount (fee.xrp ()))
    {
        JLOG(ctx.j.debug) << "preflight1: invalid fee";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

/** Checks whether the signature appears valid */
TER
preflight2 (PreflightContext const& ctx)
{
    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a
    // transaction has at least been properly signed without going to disk.
    // Each transaction also notes a source account id. This is used to verify
    // that the signing key is associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    auto const pk = RippleAddress::createAccountPublic(
        ctx.tx.getSigningPubKey());

    if(! ctx.verify(ctx.tx,
        [&, ctx] (STTx const& tx)
        {
            return (ctx.flags & tapNO_CHECK_SIGN) ||
                tx.checkSign(
                    (ctx.flags & tapENABLE_TESTING) ||
                    (ctx.rules.enabled(featureMultiSign, ctx.config.features)));
        }))
    {
        JLOG(ctx.j.debug) << "preflight2: bad signature";
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

std::uint64_t Transactor::calculateBaseFee ()
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    std::uint64_t baseFee = view().fees().units;

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::uint32_t signerCount = 0;
    if (tx().isFieldPresent (sfSigners))
        signerCount =  tx().getFieldArray (sfSigners).size();

    return baseFee + (signerCount * baseFee);
}

TER Transactor::payFee ()
{
    auto const feePaid = tx().getFieldAmount (sfFee).xrp ();
    if (!isLegalAmount (feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    // Only check fee is sufficient when the ledger is open.
    if (view().open() && feePaid < mFeeDue)
    {
        JLOG(j_.trace) << "Insufficient fee paid: " <<
            to_string (feePaid) << "/" << to_string (mFeeDue);
        return telINSUF_FEE_P;
    }

    if (feePaid == zero)
        return tesSUCCESS;

    auto const sle = view().peek(
        keylet::account(account_));

    if (mSourceBalance < feePaid)
    {
        JLOG(j_.trace) << "Insufficient balance:" <<
            " balance=" << to_string (mSourceBalance) <<
            " paid=" << to_string (feePaid);

        if ((mSourceBalance > zero) && ! view().open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back, if the transaction succeeds.

    mSourceBalance -= feePaid;
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
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle = view().peek (keylet::account(account_));

    if (sle == nullptr && account_ != zero)
    {
        JLOG (j_.trace) <<
            "apply: source account " << toBase58(account_) << " not found.";
        return terNO_ACCOUNT;
    }

    auto const& fees = view().fees();
    mFeeDue = ctx_.app.getFeeTrack().scaleFeeLoad(
        calculateBaseFee(), fees.base, fees.units, view().flags() & tapADMIN);

    if (sle)
    {
        mPriorBalance   = STAmount ((*sle)[sfBalance]).xrp ();
        mSourceBalance  = mPriorBalance;
        mHasAuthKey     = sle->isFieldPresent (sfRegularKey);

        auto terResult = checkSeq ();

        if (terResult != tesSUCCESS) return terResult;

        terResult = payFee ();

        if (terResult != tesSUCCESS) return terResult;

        terResult = checkSign ();

        if (terResult != tesSUCCESS) return terResult;

        view().update (sle);
    }

    return doApply ();
}

TER Transactor::checkSign ()
{
    // Make sure multisigning is enabled before we check for multisignatures.
    if ((view().flags() & tapENABLE_TESTING) ||
        (view().rules().enabled(featureMultiSign, ctx_.config.features)))
    {
        // If the mSigningPubKey is empty, then we must be multisigning.
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
void removeUnfundedOffers (ApplyView& view, std::vector<uint256> const& offers, beast::Journal viewJ)
{
    int removed = 0;

    for (auto const& index : offers)
    {
        auto const sleOffer = view.peek (keylet::offer (index));
        if (sleOffer)
        {
            // offer is unfunded
            offerDelete (view, sleOffer, viewJ);
            if (++removed == 1000)
                return;
        }
    }
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
    auto fee = tx().getFieldAmount(sfFee).xrp ();

    if (ctx_.size() > 5200)
        terResult = tecOVERSIZE;

    if ((terResult == tecOVERSIZE) ||
        (isTecClaim (terResult) && !(view().flags() & tapRETRY)))
    {
        // only claim the transaction fee
        JLOG(j_.debug) <<
            "Reprocessing tx " << txID << " to only claim fee";

        std::vector<uint256> removedOffers;
        if (terResult == tecOVERSIZE)
        {
            ctx_.visit (
                [&removedOffers](
                    uint256 const& index,
                    bool isDelete,
                    std::shared_ptr <SLE const> const& before,
                    std::shared_ptr <SLE const> const& after)
                {
                    if (isDelete)
                    {
                        assert (before && after);
                        if (before && after &&
                            (before->getType() == ltOFFER) &&
                            (before->getFieldAmount(sfTakerPays) == after->getFieldAmount(sfTakerPays)))
                        {
                            // Removal of offer found or made unfunded
                            removedOffers.push_back (index);
                        }
                    }
                });
        }

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
                auto const balance = txnAcct->getFieldAmount (sfBalance).xrp ();

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

                    if (terResult == tecOVERSIZE)
                       removeUnfundedOffers (view(), removedOffers, ctx_.app.journal ("View"));

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
            // Charge whatever fee they specified.

            // The transactor guarantees this will never trigger
            if (fee < zero)
            {
                // VFALCO Log to journal here
                // JLOG(journal.fatal) << "invalid fee";
                throw std::logic_error("amount is negative!");
            }

            if (fee != zero)
                ctx_.destroyXRP (fee);
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
