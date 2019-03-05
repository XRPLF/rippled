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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

/** Performs early sanity checks on the txid */
NotTEC
preflight0(PreflightContext const& ctx)
{
    auto const txID = ctx.tx.getTransactionID();

    if (txID == beast::zero)
    {
        JLOG(ctx.j.warn()) <<
            "applyTransaction: transaction id may not be zero";
        return temINVALID;
    }

    return tesSUCCESS;
}

/** Performs early sanity checks on the account and fee fields */
NotTEC
preflight1 (PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const id = ctx.tx.getAccountID(sfAccount);
    if (id == beast::zero)
    {
        JLOG(ctx.j.warn()) << "preflight1: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount (sfFee);
    if (!fee.native () || fee.negative () || !isLegalAmount (fee.xrp ()))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid fee";
        return temBAD_FEE;
    }

    auto const spk = ctx.tx.getSigningPubKey();

    if (!spk.empty () && !publicKeyType (makeSlice (spk)))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid signing key";
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

/** Checks whether the signature appears valid */
NotTEC
preflight2 (PreflightContext const& ctx)
{
    auto const sigValid = checkValidity(ctx.app.getHashRouter(),
        ctx.tx, ctx.rules, ctx.app.config());
    if (sigValid.first == Validity::SigBad)
    {
        JLOG(ctx.j.debug()) <<
            "preflight2: bad signature. " << sigValid.second;
        return temINVALID;
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

PreflightContext::PreflightContext(Application& app_, STTx const& tx_,
    Rules const& rules_, ApplyFlags flags_,
        beast::Journal j_)
    : app(app_)
    , tx(tx_)
    , rules(rules_)
    , flags(flags_)
    , j(j_)
{
}

//------------------------------------------------------------------------------

Transactor::Transactor(
    ApplyContext& ctx)
    : ctx_ (ctx)
    , j_ (ctx.journal)
{
}

std::uint64_t Transactor::calculateBaseFee (
    ReadView const& view,
    STTx const& tx)
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    std::uint64_t baseFee = view.fees().units;

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::uint32_t signerCount = 0;
    if (tx.isFieldPresent (sfSigners))
        signerCount = tx.getFieldArray (sfSigners).size();

    return baseFee + (signerCount * baseFee);
}

XRPAmount
Transactor::calculateFeePaid(STTx const& tx)
{
    return tx[sfFee].xrp();
}

XRPAmount
Transactor::minimumFee (Application& app, std::uint64_t baseFee,
    Fees const& fees, ApplyFlags flags)
{
    return scaleFeeLoad (baseFee, app.getFeeTrack (),
        fees, flags & tapUNLIMITED);
}

XRPAmount
Transactor::calculateMaxSpend(STTx const& tx)
{
    return beast::zero;
}

TER
Transactor::checkFee (PreclaimContext const& ctx,
    std::uint64_t baseFee)
{
    auto const feePaid = calculateFeePaid(ctx.tx);
    if (!isLegalAmount (feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    auto const feeDue = minimumFee(ctx.app,
        baseFee, ctx.view.fees(), ctx.flags);

    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open() && feePaid < feeDue)
    {
        JLOG(ctx.j.trace()) << "Insufficient fee paid: " <<
            to_string (feePaid) << "/" << to_string (feeDue);
        return telINSUF_FEE_P;
    }

    if (feePaid == beast::zero)
        return tesSUCCESS;

    auto const id = ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view.read(
        keylet::account(id));
    auto const balance = (*sle)[sfBalance].xrp();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace()) << "Insufficient balance:" <<
            " balance=" << to_string(balance) <<
            " paid=" << to_string(feePaid);

        if ((balance > beast::zero) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

TER Transactor::payFee ()
{
    auto const feePaid = calculateFeePaid(ctx_.tx);

    auto const sle = view().peek(
        keylet::account(account_));

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back if the transaction succeeds.

    mSourceBalance -= feePaid;
    sle->setFieldAmount (sfBalance, mSourceBalance);

    // VFALCO Should we call view().rawDestroyXRP() here as well?

    return tesSUCCESS;
}

NotTEC
Transactor::checkSeq (PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(
        keylet::account(id));

    if (!sle)
    {
        JLOG(ctx.j.trace()) <<
            "applyTransaction: delay: source account does not exist " <<
            toBase58(ctx.tx.getAccountID(sfAccount));
        return terNO_ACCOUNT;
    }

    std::uint32_t const t_seq = ctx.tx.getSequence ();
    std::uint32_t const a_seq = sle->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            JLOG(ctx.j.trace()) <<
                "applyTransaction: has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (ctx.view.txExists(ctx.tx.getTransactionID ()))
            return tefALREADY;

        JLOG(ctx.j.trace()) << "applyTransaction: has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

    if (ctx.tx.isFieldPresent (sfAccountTxnID) &&
            (sle->getFieldH256 (sfAccountTxnID) != ctx.tx.getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (ctx.tx.isFieldPresent (sfLastLedgerSequence) &&
            (ctx.view.seq() > ctx.tx.getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    return tesSUCCESS;
}

void
Transactor::setSeq ()
{
    auto const sle = view().peek(
        keylet::account(account_));

    std::uint32_t const t_seq = ctx_.tx.getSequence ();

    sle->setFieldU32 (sfSequence, t_seq + 1);

    if (sle->isFieldPresent (sfAccountTxnID))
        sle->setFieldH256 (sfAccountTxnID, ctx_.tx.getTransactionID ());
}

// check stuff before you bother to lock the ledger
void Transactor::preCompute ()
{
    account_ = ctx_.tx.getAccountID(sfAccount);
    assert(account_ != beast::zero);
}

TER Transactor::apply ()
{
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle = view().peek (keylet::account(account_));

    // sle must exist except for transactions
    // that allow zero account.
    assert(sle != nullptr || account_ == beast::zero);

    if (sle)
    {
        mPriorBalance   = STAmount ((*sle)[sfBalance]).xrp ();
        mSourceBalance  = mPriorBalance;

        setSeq();

        auto result = payFee ();

        if (result  != tesSUCCESS)
            return result;

        view().update (sle);
    }

    return doApply ();
}

NotTEC
Transactor::checkSign (PreclaimContext const& ctx)
{
    // Make sure multisigning is enabled before we check for multisignatures.
    if (ctx.view.rules().enabled(featureMultiSign))
    {
        // If the pk is empty, then we must be multi-signing.
        if (ctx.tx.getSigningPubKey().empty ())
            return checkMultiSign (ctx);
    }

    return checkSingleSign (ctx);
}

NotTEC
Transactor::checkSingleSign (PreclaimContext const& ctx)
{
    // Check that the value in the signing key slot is a public key.
    auto const pkSigner = ctx.tx.getSigningPubKey();
    if (!publicKeyType(makeSlice(pkSigner)))
    {
        JLOG(ctx.j.trace()) <<
            "checkSingleSign: signing public key type is unknown";
        return tefBAD_AUTH; // FIXME: should be better error!
    }

    // Look up the account.
    auto const idSigner = calcAccountID(PublicKey(makeSlice(pkSigner)));
    auto const idAccount = ctx.tx.getAccountID(sfAccount);
    auto const sleAccount = ctx.view.read(keylet::account(idAccount));
    bool const isMasterDisabled = sleAccount->isFlag(lsfDisableMaster);

    if (ctx.view.rules().enabled(fixMasterKeyAsRegularKey))
    {

        // Signed with regular key.
        if ((*sleAccount)[~sfRegularKey] == idSigner)
        {
            return tesSUCCESS;
        }

        // Signed with enabled mater key.
        if (!isMasterDisabled && idAccount == idSigner)
        {
            return tesSUCCESS;
        }

        // Signed with disabled master key.
        if (isMasterDisabled && idAccount == idSigner)
        {
            return tefMASTER_DISABLED;
        }

        // Signed with any other key.
        return tefBAD_AUTH;

    }

    if (idSigner == idAccount)
    {
        // Signing with the master key. Continue if it is not disabled.
        if (isMasterDisabled)
            return tefMASTER_DISABLED;
    }
    else if ((*sleAccount)[~sfRegularKey] == idSigner)
    {
        // Signing with the regular key. Continue.
    }
    else if (sleAccount->isFieldPresent(sfRegularKey))
    {
        // Signing key does not match master or regular key.
        JLOG(ctx.j.trace()) <<
            "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        // No regular key on account and signing key does not match master key.
        // FIXME: Why differentiate this case from tefBAD_AUTH?
        JLOG(ctx.j.trace()) <<
            "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

NotTEC
Transactor::checkMultiSign (PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);
    // Get mTxnAccountID's SignerList and Quorum.
    std::shared_ptr<STLedgerEntry const> sleAccountSigners =
        ctx.view.read (keylet::signers(id));
    // If the signer list doesn't exist the account is not multi-signing.
    if (!sleAccountSigners)
    {
        JLOG(ctx.j.trace()) <<
            "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // We have plans to support multiple SignerLists in the future.  The
    // presence and defaulted value of the SignerListID field will enable that.
    assert (sleAccountSigners->isFieldPresent (sfSignerListID));
    assert (sleAccountSigners->getFieldU32 (sfSignerListID) == 0);

    auto accountSigners =
        SignerEntries::deserialize (*sleAccountSigners, ctx.j, "ledger");
    if (accountSigners.second != tesSUCCESS)
        return accountSigners.second;

    // Get the array of transaction signers.
    STArray const& txSigners (ctx.tx.getFieldArray (sfSigners));

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
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            JLOG(ctx.j.trace()) <<
                "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        auto const spk = txSigner.getFieldVL (sfSigningPubKey);

        if (!publicKeyType (makeSlice(spk)))
        {
            JLOG(ctx.j.trace()) <<
                "checkMultiSign: signing public key type is unknown";
            return tefBAD_SIGNATURE;
        }

        AccountID const signingAcctIDFromPubKey =
            calcAccountID(PublicKey (makeSlice(spk)));

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
        auto sleTxSignerRoot =
            ctx.view.read (keylet::account(txSignerAcctID));

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
                    JLOG(ctx.j.trace()) <<
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
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Non-phantom signer lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent (sfRegularKey))
            {
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey !=
                sleTxSignerRoot->getAccountID (sfRegularKey))
            {
                JLOG(ctx.j.trace()) <<
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
        JLOG(ctx.j.trace()) <<
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
        if (auto const sleOffer = view.peek (keylet::offer (index)))
        {
            // offer is unfunded
            offerDelete (view, sleOffer, viewJ);
            if (++removed == unfundedOfferRemoveLimit)
                return;
        }
    }
}

/** Reset the context, discarding any changes made and adjust the fee */
XRPAmount
Transactor::reset(XRPAmount fee)
{
    ctx_.discard();

    auto const txnAcct = view().peek(
        keylet::account(ctx_.tx.getAccountID(sfAccount)));

    auto const balance = txnAcct->getFieldAmount (sfBalance).xrp ();

    // balance should have already been checked in checkFee / preFlight.
    assert(balance != beast::zero && (!view().open() || balance >= fee));

    // We retry/reject the transaction if the account balance is zero or we're
    // applying against an open ledger and the balance is less than the fee
    if (fee > balance)
        fee = balance;

    // Since we reset the context, we need to charge the fee and update
    // the account's sequence number again.
    txnAcct->setFieldAmount (sfBalance, balance - fee);
    txnAcct->setFieldU32 (sfSequence, ctx_.tx.getSequence() + 1);

    view().update (txnAcct);

    return fee;
}

//------------------------------------------------------------------------------
std::pair<TER, bool>
Transactor::operator()()
{
    JLOG(j_.trace()) << "apply: " << ctx_.tx.getTransactionID ();

#ifdef BEAST_DEBUG
    {
        Serializer ser;
        ctx_.tx.add (ser);
        SerialIter sit(ser.slice());
        STTx s2 (sit);

        if (! s2.isEquivalent(ctx_.tx))
        {
            JLOG(j_.fatal()) <<
                "Transaction serdes mismatch";
            JLOG(j_.info()) << to_string(ctx_.tx.getJson (0));
            JLOG(j_.fatal()) << s2.getJson (0);
            assert (false);
        }
    }
#endif

    auto result = ctx_.preclaimResult;
    if (result == tesSUCCESS)
        result = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    assert(result != temUNKNOWN);

    if (auto stream = j_.trace())
        stream << "preclaim result: " << transToken(result);

    bool applied = isTesSuccess (result);
    auto fee = ctx_.tx.getFieldAmount(sfFee).xrp ();

    if (ctx_.size() > oversizeMetaDataCap)
        result = tecOVERSIZE;

    if ((result == tecOVERSIZE) || (result == tecKILLED) ||
        (isTecClaimHardFail (result, view().flags())))
    {
        JLOG(j_.trace()) << "reapplying because of " << transToken(result);

        std::vector<uint256> removedOffers;

        if ((result == tecOVERSIZE) || (result == tecKILLED))
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

        // Reset the context, potentially adjusting the fee
        fee = reset(fee);

        // If necessary, remove any offers found unfunded during processing
        if ((result == tecOVERSIZE) || (result == tecKILLED))
            removeUnfundedOffers (view(), removedOffers, ctx_.app.journal ("View"));

        applied = true;
    }

    if (applied)
    {
        // Check invariants: if `tecINVARIANT_FAILED` is not returned, we can
        // proceed to apply the tx
        result = ctx_.checkInvariants(result, fee);

        if (result == tecINVARIANT_FAILED)
        {
            // if invariants checking failed again, reset the context and
            // attempt to only claim a fee.
            fee = reset(fee);

            // Check invariants again to ensure the fee claiming doesn't
            // violate invariants.
            result = ctx_.checkInvariants(result, fee);
        }

        // We ran through the invariant checker, which can, in some cases,
        // return a tef error code. Don't apply the transaction in that case.
        if (!isTecClaim(result) && !isTesSuccess(result))
            applied = false;
    }

    if (applied)
    {
        // Transaction succeeded fully or (retries are not allowed and the
        // transaction could claim a fee)

        // The transactor and invariant checkers guarantee that this will
        // *never* trigger but if it, somehow, happens, don't allow a tx
        // that charges a negative fee.
        if (fee < beast::zero)
            Throw<std::logic_error> ("fee charged is negative!");

        // Charge whatever fee they specified. The fee has already been
        // deducted from the balance of the account that issued the
        // transaction. We just need to account for it in the ledger
        // header.
        if (!view().open() && fee != beast::zero)
            ctx_.destroyXRP (fee);

        // Once we call apply, we will no longer be able to look at view()
        ctx_.apply(result);
    }

    JLOG(j_.trace()) << (applied ? "applied" : "not applied") << transToken(result);

    return { result, applied };
}

}
