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

#include <ripple/app/hook/applyHook.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/detail/ApplyViewBase.h>
#include <ripple/app/hook/Enum.h>
#include <limits>
#include <set>

namespace ripple {

/** Performs early sanity checks on the txid */
NotTEC
preflight0(PreflightContext const& ctx)
{
    auto const txID = ctx.tx.getTransactionID();

    if (txID == beast::zero)
    {
        JLOG(ctx.j.warn())
            << "applyTransaction: transaction id may not be zero";
        return temINVALID;
    }

    return tesSUCCESS;
}

/** Performs early sanity checks on the account and fee fields */
NotTEC
preflight1(PreflightContext const& ctx)
{
    // This is inappropriate in preflight0, because only Change transactions
    // skip this function, and those do not allow an sfTicketSequence field.
    if (ctx.tx.isFieldPresent(sfTicketSequence) &&
        !ctx.rules.enabled(featureTicketBatch))
    {
        return temMALFORMED;
    }

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
    auto const fee = ctx.tx.getFieldAmount(sfFee);
    if (!fee.native() || fee.negative() || !isLegalAmount(fee.xrp()))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid fee";
        return temBAD_FEE;
    }

    // if a hook emitted this transaction we bypass signature checks
    // there is a bar to circularing emitted transactions on the network
    // in their prevalidated form so this is safe
    if (ctx.rules.enabled(featureHooks) &&
        hook::isEmittedTxn(ctx.tx))
        return tesSUCCESS;

    auto const spk = ctx.tx.getSigningPubKey();

    if (!spk.empty() && !publicKeyType(makeSlice(spk)))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid signing key";
        return temBAD_SIGNATURE;
    }

    // An AccountTxnID field constrains transaction ordering more than the
    // Sequence field.  Tickets, on the other hand, reduce ordering
    // constraints.  Because Tickets and AccountTxnID work against one
    // another the combination is unsupported and treated as malformed.
    //
    // We return temINVALID for such transactions.
    if (ctx.tx.getSeqProxy().isTicket() &&
        ctx.tx.isFieldPresent(sfAccountTxnID))
        return temINVALID;

    return tesSUCCESS;
}

/** Checks whether the signature appears valid */
NotTEC
preflight2(PreflightContext const& ctx)
{
    auto const sigValid = checkValidity(
        ctx.app.getHashRouter(), ctx.tx, ctx.rules, ctx.app.config());
    if (sigValid.first == Validity::SigBad)
    {
        JLOG(ctx.j.debug()) << "preflight2: bad signature. " << sigValid.second;
        return temINVALID;
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

PreflightContext::PreflightContext(
    Application& app_,
    STTx const& tx_,
    Rules const& rules_,
    ApplyFlags flags_,
    beast::Journal j_)
    : app(app_), tx(tx_), rules(rules_), flags(flags_), j(j_)
{
}

//------------------------------------------------------------------------------

Transactor::Transactor(ApplyContext& ctx)
    : ctx_(ctx), j_(ctx.journal), account_(ctx.tx.getAccountID(sfAccount))
{
}


// RH NOTE: this only computes one chain at a time, so if there is a receiving side to a txn
// then it must seperately be computed by a second call here
FeeUnit64
Transactor::
calculateHookChainFee(
    ReadView const& view,
    STTx const& tx,
    Keylet const& hookKeylet,
    bool collectCallsOnly)
{

    std::shared_ptr<SLE const> hookSLE = view.read(hookKeylet);
    if (!hookSLE)
        return FeeUnit64{0};

    FeeUnit64 fee{0};

    auto const& hooks = hookSLE->getFieldArray(sfHooks);

    for (auto const& hook : hooks)
    {
        ripple::STObject const* hookObj = dynamic_cast<ripple::STObject const*>(&hook);

        if (!hookObj->isFieldPresent(sfHookHash)) // skip blanks
            continue;
        
        uint256 const& hash = hookObj->getFieldH256(sfHookHash);
            
        std::shared_ptr<SLE const> hookDef = view.read(keylet::hookDefinition(hash));

        // this is an edge case that happens when a hook is deleted and executed at the same ledger
        // the fee calculation for it can no longer occur
        if (!hookDef)
        {
            printf("calculateHookChainFee edge case\n");
            continue;
        }
        
        // check if the hook can fire
        uint64_t hookOn = (hookObj->isFieldPresent(sfHookOn)
                ? hookObj->getFieldU64(sfHookOn)
                : hookDef->getFieldU64(sfHookOn));

        uint32_t flags = 0;
        if (hookObj->isFieldPresent(sfFlags))
            flags = hookObj->getFieldU32(sfFlags);
        else
            flags = hookDef->getFieldU32(sfFlags);

        if (hook::canHook(tx.getTxnType(), hookOn) &&
            (!collectCallsOnly || (flags & hook::hsfCOLLECT)))
        {
                fee += FeeUnit64{
                    (uint32_t)(hookDef->getFieldAmount(sfFee).xrp().drops())
                };
        }
    }

    return fee;
}

FeeUnit64
Transactor::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    FeeUnit64 const baseFee = safe_cast<FeeUnit64>(view.fees().units);

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 0;

    FeeUnit64 hookExecutionFee{0};
    uint64_t burden {1};
    if (view.rules().enabled(featureHooks))
    {
        // if this is a "cleanup" txn we regard it as already paid up
        if (tx.getFieldU16(sfTransactionType) == ttEMIT_FAILURE)
            return FeeUnit64{0};    

        // if the txn is an emitted txn then we add the callback fee
        // if the txn is NOT an emitted txn then we process the sending account's hook chain 
        if (tx.isFieldPresent(sfEmitDetails))
        {
            STObject const& emitDetails = 
                const_cast<ripple::STTx&>(tx).getField(sfEmitDetails).downcast<STObject>();
         
            uint256 const& callbackHookHash = emitDetails.getFieldH256(sfEmitHookHash);

            std::shared_ptr<SLE const> hookDef = view.read(keylet::hookDefinition(callbackHookHash));

            if (hookDef && hookDef->isFieldPresent(sfHookCallbackFee))
                hookExecutionFee +=
                    FeeUnit64{(uint32_t)(hookDef->getFieldAmount(sfHookCallbackFee).xrp().drops())};

            assert (emitDetails.isFieldPresent(sfEmitBurden));

            burden = emitDetails.getFieldU64(sfEmitBurden);
        }
        else
            hookExecutionFee +=
                calculateHookChainFee(view, tx, keylet::hook(tx.getAccountID(sfAccount)));

        // find any additional stakeholders whose hooks will be executed and charged to this transaction
        std::vector<std::pair<AccountID, bool>> tsh =
            hook::getTransactionalStakeHolders(tx, view);

        for (auto& [tshAcc, canRollback] : tsh)
            if (canRollback)
                hookExecutionFee +=
                    calculateHookChainFee(view, tx, keylet::hook(tshAcc));
    }

    // RH NOTE: hookExecutionFee = 0, burden = 1 if hooks is not enabled 
    return baseFee * burden + (signerCount * baseFee) + hookExecutionFee; 
}

XRPAmount
Transactor::minimumFee(
    Application& app,
    FeeUnit64 baseFee,
    Fees const& fees,
    ApplyFlags flags)
{
    return scaleFeeLoad(baseFee, app.getFeeTrack(), fees, flags & tapUNLIMITED);
}

TER
Transactor::checkFee(PreclaimContext const& ctx, FeeUnit64 baseFee)
{
    if (!ctx.tx[sfFee].native())
        return temBAD_FEE;

    auto const feePaid = ctx.tx[sfFee].xrp();
    if (!isLegalAmount(feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open())
    {
        auto const feeDue =
            minimumFee(ctx.app, baseFee, ctx.view.fees(), ctx.flags);

        if (feePaid < feeDue)
        {
            JLOG(ctx.j.trace())
                << "Insufficient fee paid: " << to_string(feePaid) << "/"
                << to_string(feeDue);
            return telINSUF_FEE_P;
        }
    }

    if (feePaid == beast::zero)
        return tesSUCCESS;

    auto const id = ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    auto const balance = (*sle)[sfBalance].xrp();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace()) << "Insufficient balance:"
                            << " balance=" << to_string(balance)
                            << " paid=" << to_string(feePaid);

        if ((balance > beast::zero) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

TER
Transactor::payFee()
{
    auto const feePaid = ctx_.tx[sfFee].xrp();

    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back if the transaction succeeds.

    mSourceBalance -= feePaid;
    sle->setFieldAmount(sfBalance, mSourceBalance);

    // VFALCO Should we call view().rawDestroyXRP() here as well?

    return tesSUCCESS;
}

NotTEC
Transactor::checkSeqProxy(
    ReadView const& view,
    STTx const& tx,
    beast::Journal j)
{
    auto const id = tx.getAccountID(sfAccount);

    auto const sle = view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(j.trace())
            << "applyTransaction: delay: source account does not exist "
            << toBase58(id);
        return terNO_ACCOUNT;
    }

    SeqProxy const t_seqProx = tx.getSeqProxy();
    SeqProxy const a_seq = SeqProxy::sequence((*sle)[sfSequence]);

    // pass all emitted tx provided their seq is 0
    if (view.rules().enabled(featureHooks) &&
        hook::isEmittedTxn(tx))
    {
        // this is more strictly enforced in the emit() hook api
        // here this is only acting as a sanity check in case of bugs
        if (!tx.isFieldPresent(sfFirstLedgerSequence))
            return tefINTERNAL;
        return tesSUCCESS;
    }

    // reserved for emitted tx only at this time
    if (tx.isFieldPresent(sfFirstLedgerSequence))
        return tefINTERNAL;

    if (t_seqProx.isSeq())
    {
        if (tx.isFieldPresent(sfTicketSequence) &&
            view.rules().enabled(featureTicketBatch))
        {
            JLOG(j.trace()) << "applyTransaction: has both a TicketSequence "
                               "and a non-zero Sequence number";
            return temSEQ_AND_TICKET;
        }
        if (t_seqProx != a_seq)
        {
            if (a_seq < t_seqProx)
            {
                JLOG(j.trace())
                    << "applyTransaction: has future sequence number "
                    << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
                return terPRE_SEQ;
            }
            // It's an already-used sequence number.
            JLOG(j.trace()) << "applyTransaction: has past sequence number "
                            << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return tefPAST_SEQ;
        }
    }
    else if (t_seqProx.isTicket())
    {
        // Bypass the type comparison. Apples and oranges.
        if (a_seq.value() <= t_seqProx.value())
        {
            // If the Ticket number is greater than or equal to the
            // account sequence there's the possibility that the
            // transaction to create the Ticket has not hit the ledger
            // yet.  Allow a retry.
            JLOG(j.trace()) << "applyTransaction: has future ticket id "
                            << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return terPRE_TICKET;
        }

        // Transaction can never succeed if the Ticket is not in the ledger.
        if (!view.exists(keylet::ticket(id, t_seqProx)))
        {
            JLOG(j.trace())
                << "applyTransaction: ticket already used or never created "
                << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return tefNO_TICKET;
        }
    }

    return tesSUCCESS;
}

NotTEC
Transactor::checkPriorTxAndLastLedger(PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(ctx.j.trace())
            << "applyTransaction: delay: source account does not exist "
            << toBase58(id);
        return terNO_ACCOUNT;
    }

    if (ctx.tx.isFieldPresent(sfAccountTxnID) &&
        (sle->getFieldH256(sfAccountTxnID) !=
         ctx.tx.getFieldH256(sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (ctx.tx.isFieldPresent(sfLastLedgerSequence) &&
        (ctx.view.seq() > ctx.tx.getFieldU32(sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    if (ctx.view.txExists(ctx.tx.getTransactionID()))
        return tefALREADY;

    return tesSUCCESS;
}

TER
Transactor::consumeSeqProxy(SLE::pointer const& sleAccount)
{
    assert(sleAccount);

    // do not update sequence of sfAccountTxnID for emitted tx
    if (ctx_.emitted()) 
        return tesSUCCESS;

    SeqProxy const seqProx = ctx_.tx.getSeqProxy();
    if (seqProx.isSeq())
    {
        // Note that if this transaction is a TicketCreate, then
        // the transaction will modify the account root sfSequence
        // yet again.
        sleAccount->setFieldU32(sfSequence, seqProx.value() + 1);
        return tesSUCCESS;
    }
    return ticketDelete(
        view(), account_, getTicketIndex(account_, seqProx), j_);
}

// Remove a single Ticket from the ledger.
TER
Transactor::ticketDelete(
    ApplyView& view,
    AccountID const& account,
    uint256 const& ticketIndex,
    beast::Journal j)
{
    // Delete the Ticket, adjust the account root ticket count, and
    // reduce the owner count.
    SLE::pointer const sleTicket = view.peek(keylet::ticket(ticketIndex));
    if (!sleTicket)
    {
        JLOG(j.fatal()) << "Ticket disappeared from ledger.";
        return tefBAD_LEDGER;
    }

    std::uint64_t const page{(*sleTicket)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), page, ticketIndex, true))
    {
        JLOG(j.fatal()) << "Unable to delete Ticket from owner.";
        return tefBAD_LEDGER;
    }

    // Update the account root's TicketCount.  If the ticket count drops to
    // zero remove the (optional) field.
    auto sleAccount = view.peek(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(j.fatal()) << "Could not find Ticket owner account root.";
        return tefBAD_LEDGER;
    }

    if (auto ticketCount = (*sleAccount)[~sfTicketCount])
    {
        if (*ticketCount == 1)
            sleAccount->makeFieldAbsent(sfTicketCount);
        else
            ticketCount = *ticketCount - 1;
    }
    else
    {
        JLOG(j.fatal()) << "TicketCount field missing from account root.";
        return tefBAD_LEDGER;
    }

    // Update the Ticket owner's reserve.
    adjustOwnerCount(view, sleAccount, -1, j);

    // Remove Ticket from ledger.
    view.erase(sleTicket);
    return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
void
Transactor::preCompute()
{
    assert(account_ != beast::zero);
}

TER
Transactor::apply()
{
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle = view().peek(keylet::account(account_));

    // sle must exist except for transactions
    // that allow zero account.
    assert(sle != nullptr || account_ == beast::zero);

    if (sle)
    {
        mPriorBalance = STAmount{(*sle)[sfBalance]}.xrp();
        mSourceBalance = mPriorBalance;

        TER result = consumeSeqProxy(sle);
        if (result != tesSUCCESS)
            return result;

        result = payFee();
        if (result != tesSUCCESS)
            return result;

        if (sle->isFieldPresent(sfAccountTxnID))
            sle->setFieldH256(sfAccountTxnID, ctx_.tx.getTransactionID());

        view().update(sle);
    }

    return doApply();
}

NotTEC
Transactor::checkSign(PreclaimContext const& ctx)
{
    // hook emitted transactions do not have signatures
    if (ctx.view.rules().enabled(featureHooks) &&
        hook::isEmittedTxn(ctx.tx))
        return tesSUCCESS;

    // If the pk is empty, then we must be multi-signing.
    if (ctx.tx.getSigningPubKey().empty())
        return checkMultiSign(ctx);

    return checkSingleSign(ctx);
}

NotTEC
Transactor::checkSingleSign(PreclaimContext const& ctx)
{
    // Check that the value in the signing key slot is a public key.
    auto const pkSigner = ctx.tx.getSigningPubKey();
    if (!publicKeyType(makeSlice(pkSigner)))
    {
        JLOG(ctx.j.trace())
            << "checkSingleSign: signing public key type is unknown";
        return tefBAD_AUTH;  // FIXME: should be better error!
    }

    // Look up the account.
    auto const idSigner = calcAccountID(PublicKey(makeSlice(pkSigner)));
    auto const idAccount = ctx.tx.getAccountID(sfAccount);
    auto const sleAccount = ctx.view.read(keylet::account(idAccount));
    if (!sleAccount)
        return terNO_ACCOUNT;

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
        JLOG(ctx.j.trace())
            << "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        // No regular key on account and signing key does not match master key.
        // FIXME: Why differentiate this case from tefBAD_AUTH?
        JLOG(ctx.j.trace())
            << "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

NotTEC
Transactor::checkMultiSign(PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);
    // Get mTxnAccountID's SignerList and Quorum.
    std::shared_ptr<STLedgerEntry const> sleAccountSigners =
        ctx.view.read(keylet::signers(id));
    // If the signer list doesn't exist the account is not multi-signing.
    if (!sleAccountSigners)
    {
        JLOG(ctx.j.trace())
            << "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // We have plans to support multiple SignerLists in the future.  The
    // presence and defaulted value of the SignerListID field will enable that.
    assert(sleAccountSigners->isFieldPresent(sfSignerListID));
    assert(sleAccountSigners->getFieldU32(sfSignerListID) == 0);

    auto accountSigners =
        SignerEntries::deserialize(*sleAccountSigners, ctx.j, "ledger");
    if (!accountSigners)
        return accountSigners.error();

    // Get the array of transaction signers.
    STArray const& txSigners(ctx.tx.getFieldArray(sfSigners));

    // Walk the accountSigners performing a variety of checks and see if
    // the quorum is met.

    // Both the multiSigners and accountSigners are sorted by account.  So
    // matching multi-signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto iter = accountSigners->begin();
    for (auto const& txSigner : txSigners)
    {
        AccountID const txSignerAcctID = txSigner.getAccountID(sfAccount);

        // Attempt to match the SignerEntry with a Signer;
        while (iter->account < txSignerAcctID)
        {
            if (++iter == accountSigners->end())
            {
                JLOG(ctx.j.trace())
                    << "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            JLOG(ctx.j.trace())
                << "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        auto const spk = txSigner.getFieldVL(sfSigningPubKey);

        if (!publicKeyType(makeSlice(spk)))
        {
            JLOG(ctx.j.trace())
                << "checkMultiSign: signing public key type is unknown";
            return tefBAD_SIGNATURE;
        }

        AccountID const signingAcctIDFromPubKey =
            calcAccountID(PublicKey(makeSlice(spk)));

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
        auto sleTxSignerRoot = ctx.view.read(keylet::account(txSignerAcctID));

        if (signingAcctIDFromPubKey == txSignerAcctID)
        {
            // Either Phantom or Master.  Phantoms automatically pass.
            if (sleTxSignerRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    sleTxSignerRoot->getFieldU32(sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    JLOG(ctx.j.trace())
                        << "applyTransaction: Signer:Account lsfDisableMaster.";
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
                JLOG(ctx.j.trace()) << "applyTransaction: Non-phantom signer "
                                       "lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent(sfRegularKey))
            {
                JLOG(ctx.j.trace())
                    << "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey !=
                sleTxSignerRoot->getAccountID(sfRegularKey))
            {
                JLOG(ctx.j.trace())
                    << "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += iter->weight;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < sleAccountSigners->getFieldU32(sfSignerQuorum))
    {
        JLOG(ctx.j.trace())
            << "applyTransaction: Signers failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static void
removeUnfundedOffers(
    ApplyView& view,
    std::vector<uint256> const& offers,
    beast::Journal viewJ)
{
    int removed = 0;

    for (auto const& index : offers)
    {
        if (auto const sleOffer = view.peek(keylet::offer(index)))
        {
            // offer is unfunded
            offerDelete(view, sleOffer, viewJ);
            if (++removed == unfundedOfferRemoveLimit)
                return;
        }
    }
}

static void
removeExpiredNFTokenOffers(
    ApplyView& view,
    std::vector<uint256> const& offers,
    beast::Journal viewJ)
{
    std::size_t removed = 0;

    for (auto const& index : offers)
    {
        if (auto const offer = view.peek(keylet::nftoffer(index)))
        {
            nft::deleteTokenOffer(view, offer);
            if (++removed == expiredOfferRemoveLimit)
                return;
        }
    }
}

/** Reset the context, discarding any changes made and adjust the fee */
std::pair<TER, XRPAmount>
Transactor::reset(XRPAmount fee)
{
    ApplyViewImpl& avi = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    std::vector<STObject> hookMeta;
    avi.copyHookMetaData(hookMeta);
    ctx_.discard();
    ApplyViewImpl& avi2 = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    avi2.setHookMetaData(std::move(hookMeta));

    auto const txnAcct =
        view().peek(keylet::account(ctx_.tx.getAccountID(sfAccount)));
    if (!txnAcct)
        // The account should never be missing from the ledger.  But if it
        // is missing then we can't very well charge it a fee, can we?
        return {tefINTERNAL, beast::zero};

    auto const balance = txnAcct->getFieldAmount(sfBalance).xrp();

    // balance should have already been checked in checkFee / preFlight.
    assert(balance != beast::zero && (!view().open() || balance >= fee));

    // We retry/reject the transaction if the account balance is zero or we're
    // applying against an open ledger and the balance is less than the fee
    if (fee > balance)
        fee = balance;

    // Since we reset the context, we need to charge the fee and update
    // the account's sequence number (or consume the Ticket) again.
    //
    // If for some reason we are unable to consume the ticket or sequence
    // then the ledger is corrupted.  Rather than make things worse we
    // reject the transaction.
    txnAcct->setFieldAmount(sfBalance, balance - fee);
    TER const ter{consumeSeqProxy(txnAcct)};
    assert(isTesSuccess(ter));

    if (isTesSuccess(ter))
        view().update(txnAcct);

    return {ter, fee};
}

TER
Transactor::
executeHookChain(
    std::shared_ptr<ripple::STLedgerEntry const> const& hookSLE,
    hook::HookStateMap& stateMap,
    std::vector<hook::HookResult>& results,
    ripple::AccountID const& account,
    bool strong,
    std::shared_ptr<STObject const> const& provisionalMeta)
{
    std::set<uint256> hookSkips;
    std::map<
        uint256,
        std::map<
            std::vector<uint8_t>,
            std::vector<uint8_t>
        >> hookParamOverrides {};

    auto const& hooks = hookSLE->getFieldArray(sfHooks);
    int hook_no = 0;

    for (auto const& hook : hooks)
    {
        ripple::STObject const* hookObj = dynamic_cast<ripple::STObject const*>(&hook);

        if (!hookObj->isFieldPresent(sfHookHash)) // skip blanks
            continue;

        // lookup hook definition
        uint256 const& hookHash = hookObj->getFieldH256(sfHookHash);

        if (hookSkips.find(hookHash) != hookSkips.end())
        {
            JLOG(j_.trace())
                << "HookInfo: Skipping " << hookHash;
            continue;
        }

        auto const& hookDef = ctx_.view().peek(keylet::hookDefinition(hookHash));
        if (!hookDef)
        {
            JLOG(j_.warn())
                << "HookError[]: Failure: hook def missing (send)";
            continue;
        }

        // check if the hook can fire
        uint64_t hookOn = (hookObj->isFieldPresent(sfHookOn)
                ? hookObj->getFieldU64(sfHookOn)
                : hookDef->getFieldU64(sfHookOn));

        if (!hook::canHook(ctx_.tx.getTxnType(), hookOn))
            continue;    // skip if it can't

        uint32_t flags = (hookObj->isFieldPresent(sfFlags) ?
                hookObj->getFieldU32(sfFlags) : hookDef->getFieldU32(sfFlags));

        JLOG(j_.trace())
            << "HookChainExecution: " << hookHash
            << " strong:" << strong << " flags&hsfCOLLECT: " << (flags & hsfCOLLECT); 

        // skip weakly executed hooks that lack a collect flag
        if (!strong && !(flags & hsfCOLLECT))
            continue;

        // fetch the namespace either from the hook object of, if absent, the hook def
        uint256 const& ns = 
            (hookObj->isFieldPresent(sfHookNamespace)
                 ?  hookObj->getFieldH256(sfHookNamespace)
                 :  hookDef->getFieldH256(sfHookNamespace));

        // gather parameters
        std::map<std::vector<uint8_t>, std::vector<uint8_t>> parameters;
        if (hook::gatherHookParameters(hookDef, hookObj, parameters, j_))
        {
            JLOG(j_.warn())
                << "HookError[]: Failure: gatherHookParameters failed)";
            return tecINTERNAL;
        }

        bool hasCallback = hookDef->isFieldPresent(sfHookCallbackFee);

        results.push_back(
            hook::apply(
                hookDef->getFieldH256(sfHookSetTxnID),
                hookHash,
                ns,
                hookDef->getFieldVL(sfCreateCode),
                parameters,
                hookParamOverrides,
                stateMap,
                ctx_,
                account,
                hasCallback,
                false,
                strong,
                (strong ? 0 : 1UL),             // 0 = strong, 1 = weak
                hook_no,
                provisionalMeta));

        executedHookCount_++;

        hook::HookResult& hookResult = results.back();

        if (hookResult.exitType != hook_api::ExitType::ACCEPT)
        {
            if (results.back().exitType == hook_api::ExitType::WASM_ERROR)
                return temMALFORMED;
            return tecHOOK_REJECTED;
        }

        // gather skips
        for (uint256 const& hash : hookResult.hookSkips)
            if (hookSkips.find(hash) == hookSkips.end())
                hookSkips.emplace(hash);

        // gather overrides
        auto const& resultOverrides = hookResult.hookParamOverrides;
        for (auto const& [hash, params] : resultOverrides)
        {
            if (hookParamOverrides.find(hash) == hookParamOverrides.end())
                hookParamOverrides[hash] = {};

            auto& overrides = hookParamOverrides[hash];
            for (auto const& [k, v] : params)
                overrides[k] = v;
        }

        hook_no++;
    }
    return tesSUCCESS;
}

void
Transactor::doHookCallback(std::shared_ptr<STObject const> const& provisionalMeta)
{
    // Finally check if there is a callback
    if (!ctx_.tx.isFieldPresent(sfEmitDetails))
        return;

    auto const& emitDetails =
        const_cast<ripple::STTx&>(ctx_.tx).getField(sfEmitDetails).downcast<STObject>();

    // callbacks are optional so if there isn't a callback then skip
    if (!emitDetails.isFieldPresent(sfEmitCallback))
        return;

    AccountID const& callbackAccountID = emitDetails.getAccountID(sfEmitCallback);
    uint256 const& callbackHookHash = emitDetails.getFieldH256(sfEmitHookHash);

    auto const& hooksCallback = view().peek(keylet::hook(callbackAccountID));
    auto const& hookDef = view().peek(keylet::hookDefinition(callbackHookHash));
    if (!hookDef)
    {
        JLOG(j_.warn())
            << "HookError[]: Hook def missing on callback";
        return;
    }

    if (!hookDef->isFieldPresent(sfHookCallbackFee))
    {
        JLOG(j_.trace())
            << "HookInfo[" << callbackAccountID << "]: Callback specified by emitted txn "
            << "but hook lacks a cbak function, skipping.";
        return;
    }

    if (!hooksCallback)
    {
        JLOG(j_.warn())
            << "HookError[]: Hook missing on callback";
        return;
    }

    if (!hooksCallback->isFieldPresent(sfHooks))
    {
        JLOG(j_.warn())
            << "HookError[]: Hooks Array missing on callback";
        return;
    }

    bool found = false;
    auto const& hooks = hooksCallback->getFieldArray(sfHooks);
    int hook_no = 0;
    for (auto const& hook : hooks)
    {
        hook_no++;

        STObject const* hookObj = dynamic_cast<STObject const*>(&hook);

        if (!hookObj->isFieldPresent(sfHookHash)) // skip blanks
            continue;

        if (hookObj->getFieldH256(sfHookHash) != callbackHookHash)
            continue;
    
        // fetch the namespace either from the hook object of, if absent, the hook def
        uint256 const& ns = 
            (hookObj->isFieldPresent(sfHookNamespace)
                 ?  hookObj->getFieldH256(sfHookNamespace)
                 :  hookDef->getFieldH256(sfHookNamespace));

        executedHookCount_++;

        std::map<std::vector<uint8_t>, std::vector<uint8_t>> parameters;
        if (hook::gatherHookParameters(hookDef, hookObj, parameters, j_))
        {
            JLOG(j_.warn())
                << "HookError[]: Failure: gatherHookParameters failed)";
            return;
        }

        found = true;

        // this call will clean up ltEMITTED_NODE as well
        try
        {

            hook::HookStateMap stateMap;

            hook::HookResult callbackResult = 
                hook::apply(
                    hookDef->getFieldH256(sfHookSetTxnID),
                    callbackHookHash,
                    ns,
                    hookDef->getFieldVL(sfCreateCode),
                    parameters,
                    {},
                    stateMap,
                    ctx_,
                    callbackAccountID,
                    true,
                    true,
                    false,
                    safe_cast<TxType>(ctx_.tx.getFieldU16(sfTransactionType)) == ttEMIT_FAILURE 
                        ? 1UL : 0UL, 
                    hook_no - 1,
                    provisionalMeta);

            
            bool success = callbackResult.exitType == hook_api::ExitType::ACCEPT;

            // write any state changes if cbak resulted in accept()
            if (success)
                hook::finalizeHookState(stateMap, ctx_, ctx_.tx.getTransactionID());

            // write the final result
            ripple::TER result =
                finalizeHookResult(callbackResult, ctx_, success);

            JLOG(j_.trace())
                << "HookInfo[" << callbackAccountID << "-" <<ctx_.tx.getAccountID(sfAccount) << "]: "
                << "Callback finalizeHookResult = "
                << result;

        }
        catch (std::exception& e)
        {
            JLOG(j_.fatal()) 
                << "HookError[" << callbackAccountID << "-" <<ctx_.tx.getAccountID(sfAccount) << "]: "
                << "]: Callback failure " << e.what();
        }

    }

    if (!found)
    {
        JLOG(j_.warn())
            << "HookError[" << callbackAccountID << "]: Hookhash "
            << callbackHookHash << " not found on callback account";
    }
}

void
Transactor::
addWeakTSHFromSandbox(detail::ApplyViewBase const& pv)
{
    // If Hooks are enabled then non-issuers who have their TL balance
    // modified by the execution of the path have the opportunity to have their
    // weak hooks executed.
    if (ctx_.view().rules().enabled(featureHooks))
    {
        // anyone whose balance changed as a result of this Pathing is a weak TSH
        auto bc = pv.balanceChanges(view());

        for (auto const& entry : bc)
        {
            std::tuple<AccountID, AccountID, Currency> const& tpl = entry.first;
            Currency const& cur = std::get<2>(tpl);
            if (isXRP(cur))
                continue;

            AccountID const& lowAcc = std::get<0>(tpl);
            AccountID const& highAcc = std::get<1>(tpl);
            STAmount const& amt = entry.second;
            additionalWeakTSH_.emplace(amt >= beast::zero ? lowAcc : highAcc);
        }
    }
}

TER
Transactor::
doTSH(
    bool strong,                    // only strong iff true, only weak iff false
    hook::HookStateMap& stateMap,
    std::vector<hook::HookResult>& results,
    std::shared_ptr<STObject const> const& provisionalMeta)
{
    auto& view = ctx_.view();

    std::vector<std::pair<AccountID, bool>> tsh = 
        hook::getTransactionalStakeHolders(ctx_.tx, view);

    // add the extra TSH marked out by the specific transactor (if applicable)
    if (!strong)
        for (auto& weakTsh : additionalWeakTSH_)
            tsh.emplace_back(weakTsh, false);

    // we use a vector above for order preservation
    // but we also don't want to execute any hooks
    // twice, so keep track as we go with a map
    std::set<AccountID> alreadyProcessed;

    for (auto& [tshAccountID, canRollback] : tsh)
    {
        // this isn't an error because transactors may
        // blindly nominate any TSHes they find but
        // obviously we will never execute OTXN account
        // as a TSH because they already had first execution
        if (tshAccountID == account_)
            continue;

        if (alreadyProcessed.find(tshAccountID) != alreadyProcessed.end())
            continue;

        alreadyProcessed.emplace(tshAccountID);

        // only process the relevant ones
        if ((!canRollback && strong) || (canRollback && !strong))
            continue;

        auto klTshHook = keylet::hook(tshAccountID);

        auto tshHook = view.read(klTshHook);
        if (!(tshHook && tshHook->isFieldPresent(sfHooks)))
            continue;

        // scoping here allows tshAcc to leave scope before
        // hook execution, which is probably safer
        {
            // check if the TSH exists and/or has any hooks
            auto tshAcc = view.peek(keylet::account(tshAccountID));
            if (!tshAcc)
                continue;

            // compute and deduct fees for the TSH if applicable
            FeeUnit64 tshFee =
                calculateHookChainFee(view, ctx_.tx, klTshHook, !canRollback);

            // no hooks to execute, skip tsh
            if (tshFee == 0)
                continue;
                
            XRPAmount tshFeeDrops = view.fees().toDrops(tshFee);
            assert(tshFeeDrops >= beast::zero);

            STAmount priorBalance = tshAcc->getFieldAmount(sfBalance);

            if (canRollback)
            {
                // this is not a collect call so we will force the tsh's fee to 0
                // the otxn paid the fee for this tsh chain execution already.
                tshFeeDrops = 0;
            }
            else
            {
                // this is a collect call so first check if the tsh can accept
                uint32_t tshFlags = tshAcc->getFieldU32(sfFlags);
                if (!canRollback && !(tshFlags & lsfTshCollect))
                {
                    // this TSH doesn't allow collect calls, skip
                    JLOG(j_.trace())
                        << "HookInfo[" << account_ << "]: TSH acc " << tshAccountID << " "
                        << "hook chain execution skipped due to lack of lsfTshCollect flag.";
                    continue;
                }

                // now check if they can afford this collect call
                auto const uOwnerCount = tshAcc->getFieldU32(sfOwnerCount);
                auto const reserve = view.fees().accountReserve(uOwnerCount);

                if (tshFeeDrops + reserve > priorBalance)
                {
                    JLOG(j_.trace())
                        << "HookInfo[" << account_ << "]: TSH acc " << tshAccountID << " "
                        << "hook chain execution skipped due to lack of TSH acc funds.";
                    continue;
                }
            }

            if (tshFeeDrops > beast::zero)
            {
                STAmount finalBalance = priorBalance - tshFeeDrops;
                assert(finalBalance >= beast::zero);
                assert(finalBalance < priorBalance);

                tshAcc->setFieldAmount(sfBalance, finalBalance);
                view.update(tshAcc);
                ctx_.destroyXRP(tshFeeDrops);
            }
        }

        // execution to here means we can run the TSH's hook chain
        TER tshResult =
            executeHookChain(
                tshHook,
                stateMap,
                results,
                tshAccountID,
                strong,
                provisionalMeta);

        if (canRollback && (tshResult != tesSUCCESS))
            return tshResult;
    }

    return tesSUCCESS;
}

void
Transactor::doAaw(
    AccountID const& hookAccountID,
    std::set<uint256> const& hookHashes,
    hook::HookStateMap& stateMap,
    std::vector<hook::HookResult>& results,
    std::shared_ptr<STObject const> const& provisionalMeta)
{

    auto const& hooksArray = view().peek(keylet::hook(hookAccountID));
    if (!hooksArray)
    {
        JLOG(j_.warn())
            << "HookError[]: Hook missing on aaw account: "
            << hookAccountID;
        return;
    }

    if (!hooksArray->isFieldPresent(sfHooks))
    {
        JLOG(j_.warn())
            << "HookError[]: Hooks Array missing on aaw";
        return;
    }

    auto const& hooks = hooksArray->getFieldArray(sfHooks);
    int hook_no = 0;
    for (auto const& hook : hooks)
    {
        hook_no++;

        STObject const* hookObj = dynamic_cast<STObject const*>(&hook);

        if (!hookObj->isFieldPresent(sfHookHash)) // skip blanks
            continue;
        
        uint256 const& hookHash = hookObj->getFieldH256(sfHookHash);

        if (hookHashes.find(hookObj->getFieldH256(sfHookHash)) == hookHashes.end())
            continue;

        auto const& hookDef = view().peek(keylet::hookDefinition(hookHash));
        if (!hookDef)
        {
            JLOG(j_.warn())
                << "HookError[]: Hook def missing on aaw, hash: "
                << hookHash;
            continue;
        }

        // fetch the namespace either from the hook object of, if absent, the hook def
        uint256 const& ns = 
            (hookObj->isFieldPresent(sfHookNamespace)
                 ?  hookObj->getFieldH256(sfHookNamespace)
                 :  hookDef->getFieldH256(sfHookNamespace));

        executedHookCount_++;

        std::map<std::vector<uint8_t>, std::vector<uint8_t>> parameters;
        if (hook::gatherHookParameters(hookDef, hookObj, parameters, j_))
        {
            JLOG(j_.warn())
                << "HookError[]: Failure: gatherHookParameters failed)";
            return;
        }

        try
        {
            hook::HookResult aawResult = 
                hook::apply(
                    hookDef->getFieldH256(sfHookSetTxnID),
                    hookHash,
                    ns,
                    hookDef->getFieldVL(sfCreateCode),
                    parameters,
                    {},
                    stateMap,
                    ctx_,
                    hookAccountID,
                    hookDef->isFieldPresent(sfHookCallbackFee),
                    false,
                    false,
                    2UL,                                            // param 2 = aaw
                    hook_no - 1,
                    provisionalMeta);


            results.push_back(aawResult);            

            JLOG(j_.trace())
                << "HookInfo[" << hookAccountID << "-" <<ctx_.tx.getAccountID(sfAccount) << "]: "
                << " aaw Hook ExitCode = "
                << aawResult.exitCode;

        }
        catch (std::exception& e)
        {
            JLOG(j_.fatal()) 
                << "HookError[" << hookAccountID << "-" <<ctx_.tx.getAccountID(sfAccount) << "]: "
                << "]: aaw failure " << e.what();
        }

    }

}

//------------------------------------------------------------------------------
std::pair<TER, bool>
Transactor::operator()()
{
    JLOG(j_.trace()) << "apply: " << ctx_.tx.getTransactionID();

    STAmountSO stAmountSO{view().rules().enabled(fixSTAmountCanonicalize)};

#ifdef DEBUG
    {
        Serializer ser;
        ctx_.tx.add(ser);
        SerialIter sit(ser.slice());
        STTx s2(sit);

        if (!s2.isEquivalent(ctx_.tx))
        {
            JLOG(j_.fatal()) << "Transaction serdes mismatch";
            JLOG(j_.info()) << to_string(ctx_.tx.getJson(JsonOptions::none));
            JLOG(j_.fatal()) << s2.getJson(JsonOptions::none);
            assert(false);
        }
    }
#endif


    auto result = ctx_.preclaimResult;
    
    bool const hooksEnabled = view().rules().enabled(featureHooks);

    // AgainAsWeak map stores information about accounts whose strongly executed hooks
    // request an additional weak execution after the otxn has finished application to the ledger
    std::map<AccountID, std::set<uint256>> aawMap;

    // Pre-application (Strong TSH) Hooks are executed here
    // These TSH have the right to rollback.
    // Weak TSH and callback are executed post-application.
    if (hooksEnabled && (result == tesSUCCESS || result == tecHOOK_REJECTED))
    {
        // this state map will be shared across all hooks in this execution chain
        // and any associated chains which are executed during this transaction also
        // this map can get large so 
        hook::HookStateMap stateMap;

        auto const& accountID = ctx_.tx.getAccountID(sfAccount);
        std::vector<hook::HookResult> hookResults;

        auto const& hooksOriginator = view().read(keylet::hook(accountID));

        // First check if the Sending account has any hooks that can be fired
        if (hooksOriginator && hooksOriginator->isFieldPresent(sfHooks) && !ctx_.emitted())
            result =
                executeHookChain(
                    hooksOriginator,
                    stateMap,
                    hookResults,
                    accountID,
                    true,
                    {});

        if (isTesSuccess(result))
        {
            // Next check if there are any transactional stake holders whose hooks need to be executed
            // here. Note these are only strong TSH (who have the right to rollback the txn),
            // any weak TSH will be executed after doApply has been successful (callback as well)

            result = doTSH(true, stateMap, hookResults, {});
        }

        // write state if all chains executed successfully
        if (isTesSuccess(result))
            hook::finalizeHookState(stateMap, ctx_, ctx_.tx.getTransactionID());

        // write hook results
        // this happens irrespective of whether final result was a tesSUCCESS
        // because it contains error codes that any failed hooks would have
        // returned for meta

        for (auto& hookResult: hookResults)
        {
            hook::finalizeHookResult(hookResult, ctx_, isTesSuccess(result));
            if (isTesSuccess(result) && hookResult.executeAgainAsWeak)
            {
                if (aawMap.find(hookResult.account) == aawMap.end())
                    aawMap[hookResult.account] = {hookResult.hookHash};
                else
                    aawMap[hookResult.account].emplace(hookResult.hookHash);
            }
        }
    }

    // fall through allows normal apply
    if (result == tesSUCCESS)
        result = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    assert(result != temUNKNOWN);

    if (auto stream = j_.trace())
        stream << "preclaim result: " << transToken(result);

    bool applied = isTesSuccess(result);

    auto fee = ctx_.tx.getFieldAmount(sfFee).xrp();

    if (ctx_.size() > oversizeMetaDataCap)
        result = tecOVERSIZE;

    if ((isTecClaim(result) && (view().flags() & tapFAIL_HARD)))
    {
        // If the tapFAIL_HARD flag is set, a tec result
        // must not do anything
        ctx_.discard();
        applied = false;
    }
    else if (
        (result == tecOVERSIZE) || (result == tecKILLED) ||
        (result == tecEXPIRED) || (isTecClaimHardFail(result, view().flags())))
    {
        JLOG(j_.trace()) << "reapplying because of " << transToken(result);

        // FIXME: This mechanism for doing work while returning a `tec` is
        //        awkward and very limiting. A more general purpose approach
        //        should be used, making it possible to do more useful work
        //        when transactions fail with a `tec` code.
        std::vector<uint256> removedOffers;

        if ((result == tecOVERSIZE) || (result == tecKILLED))
        {
            ctx_.visit([&removedOffers](
                           uint256 const& index,
                           bool isDelete,
                           std::shared_ptr<SLE const> const& before,
                           std::shared_ptr<SLE const> const& after) {
                if (isDelete)
                {
                    assert(before && after);
                    if (before && after && (before->getType() == ltOFFER) &&
                        (before->getFieldAmount(sfTakerPays) ==
                         after->getFieldAmount(sfTakerPays)))
                    {
                        // Removal of offer found or made unfunded
                        removedOffers.push_back(index);
                    }
                }
            });
        }

        std::vector<uint256> expiredNFTokenOffers;

        if (result == tecEXPIRED)
        {
            ctx_.visit([&expiredNFTokenOffers](
                           uint256 const& index,
                           bool isDelete,
                           std::shared_ptr<SLE const> const& before,
                           std::shared_ptr<SLE const> const& after) {
                if (isDelete)
                {
                    assert(before && after);
                    if (before && after &&
                        (before->getType() == ltNFTOKEN_OFFER))
                        expiredNFTokenOffers.push_back(index);
                }
            });
        }

        // Reset the context, potentially adjusting the fee.
        {
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;
        }

        // If necessary, remove any offers found unfunded during processing
        if ((result == tecOVERSIZE) || (result == tecKILLED))
            removeUnfundedOffers(
                view(), removedOffers, ctx_.app.journal("View"));

        if (result == tecEXPIRED)
            removeExpiredNFTokenOffers(
                view(), expiredNFTokenOffers, ctx_.app.journal("View"));

        applied = isTecClaim(result);
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
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;

            // Check invariants again to ensure the fee claiming doesn't
            // violate invariants.
            if (isTesSuccess(result) || isTecClaim(result))
                result = ctx_.checkInvariants(result, fee);
        }

        // We ran through the invariant checker, which can, in some cases,
        // return a tef error code. Don't apply the transaction in that case.
        if (!isTecClaim(result) && !isTesSuccess(result))
            applied = false;
    }
    
    // Post-application (Weak TSH/AAW) Hooks are executed here.
    // These TSH do not have the ability to rollback.
    // The callback, if any, is also executed here.
    if (applied && hooksEnabled)
    {
        // weakly executed hooks have access to a provisional TxMeta
        // for this tx application.
        TxMeta meta = ctx_.generateProvisionalMeta();
        meta.setResult(result, 0);

        std::shared_ptr<STObject const>
            proMeta = std::make_shared<STObject const>(std::move(meta.getAsObject()));

        // perform callback logic if applicable
        if (ctx_.tx.isFieldPresent(sfEmitDetails))
            doHookCallback(proMeta);

        // remove emission entry if this is an emitted transaction
        hook::removeEmissionEntry(ctx_);

        // process weak TSH
        hook::HookStateMap stateMap;
        std::vector<hook::HookResult> weakResults;

        doTSH(false, stateMap, weakResults, proMeta);

        // execute any hooks that nominated for 'again as weak'
        for (auto const& [accID, hookHashes] : aawMap)
            doAaw(accID, hookHashes, stateMap, weakResults, proMeta);

        // write hook results
        hook::finalizeHookState(stateMap, ctx_, ctx_.tx.getTransactionID());
        for (auto& weakResult: weakResults)
            hook::finalizeHookResult(weakResult, ctx_, isTesSuccess(result));
    
        if (ctx_.size() > oversizeMetaDataCap)
            result = tecOVERSIZE;
    }



    if (applied)
    {
        // Transaction succeeded fully or (retries are not allowed and the
        // transaction could claim a fee)

        // The transactor and invariant checkers guarantee that this will
        // *never* trigger but if it, somehow, happens, don't allow a tx
        // that charges a negative fee.
        if (fee < beast::zero)
            Throw<std::logic_error>("fee charged is negative!");

        // Charge whatever fee they specified. The fee has already been
        // deducted from the balance of the account that issued the
        // transaction. We just need to account for it in the ledger
        // header.
        if (!view().open() && fee != beast::zero)
            ctx_.destroyXRP(fee);

        // Once we call apply, we will no longer be able to look at view()
        ctx_.apply(result);
    }

    JLOG(j_.trace()) << (applied ? "applied" : "not applied")
                     << transToken(result);

    return {result, applied};
}

}  // namespace ripple
