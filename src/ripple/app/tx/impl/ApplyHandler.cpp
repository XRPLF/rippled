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
#include <ripple/app/tx/impl/ApplyHandler.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/app/tx/validity.h>
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

namespace ripple {

//------------------------------------------------------------------------------

ApplyHandler::ApplyHandler(ApplyContext& applyCtx, TransactorExport transactor)
    : ctx(applyCtx), transactor_(transactor)
{
}

TER
ApplyHandler::payFee()
{
    auto const feePaid = ctx.tx[sfFee].xrp();

    auto const sle =
        ctx.view().peek(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sle)
        return tefINTERNAL;

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back if the transaction succeeds.

    mSourceBalance -= feePaid;
    sle->setFieldAmount(sfBalance, mSourceBalance);

    // VFALCO Should we call ctx.view().rawDestroyXRP() here as well?

    return tesSUCCESS;
}

TER
ApplyHandler::consumeSeqProxy(SLE::pointer const& sleAccount)
{
    assert(sleAccount);
    SeqProxy const seqProx = ctx.tx.getSeqProxy();
    if (seqProx.isSeq())
    {
        // Note that if this transaction is a TicketCreate, then
        // the transaction will modify the account root sfSequence
        // yet again.
        sleAccount->setFieldU32(sfSequence, seqProx.value() + 1);
        return tesSUCCESS;
    }
    return ticketDelete(
        ctx.view(),
        ctx.tx.getAccountID(sfAccount),
        getTicketIndex(ctx.tx.getAccountID(sfAccount), seqProx),
        ctx.journal);
}

// Remove a single Ticket from the ledger.
TER
ApplyHandler::ticketDelete(
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
ApplyHandler::preCompute()
{
    assert(ctx.tx.getAccountID(sfAccount) != beast::zero);
}

TER
ApplyHandler::apply()
{
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle =
        ctx.view().peek(keylet::account(ctx.tx.getAccountID(sfAccount)));

    // sle must exist except for transactions
    // that allow zero account.
    assert(sle != nullptr || ctx.tx.getAccountID(sfAccount) == beast::zero);

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
            sle->setFieldH256(sfAccountTxnID, ctx.tx.getTransactionID());

        ctx.view().update(sle);
    }

    if (transactor_.doApply == NULL)
        return tesSUCCESS;
    return transactor_.doApply(ctx, mPriorBalance, mSourceBalance);
}

//------------------------------------------------------------------------------

static void
removeUnfundedOffers2(
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
removeExpiredNFTokenOffers2(
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
ApplyHandler::reset(XRPAmount fee)
{
    ctx.discard();

    auto const txnAcct =
        ctx.view().peek(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!txnAcct)
        // The account should never be missing from the ledger.  But if it
        // is missing then we can't very well charge it a fee, can we?
        return {tefINTERNAL, beast::zero};

    auto const balance = txnAcct->getFieldAmount(sfBalance).xrp();

    // balance should have already been checked in checkFee / preFlight.
    assert(balance != beast::zero && (!ctx.view().open() || balance >= fee));

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
        ctx.view().update(txnAcct);

    return {ter, fee};
}

//------------------------------------------------------------------------------
std::pair<TER, bool>
ApplyHandler::operator()()
{
    JLOG(ctx.journal.trace()) << "apply: " << ctx.tx.getTransactionID();

    STAmountSO stAmountSO{ctx.view().rules().enabled(fixSTAmountCanonicalize)};
    NumberSO stNumberSO{ctx.view().rules().enabled(fixUniversalNumber)};

#ifdef DEBUG
    {
        Serializer ser;
        ctx.tx.add(ser);
        SerialIter sit(ser.slice());
        STTx s2(sit);

        if (!s2.isEquivalent(ctx.tx))
        {
            JLOG(ctx.journal.fatal()) << "Transaction serdes mismatch";
            JLOG(ctx.journal.info())
                << to_string(ctx.tx.getJson(JsonOptions::none));
            JLOG(ctx.journal.fatal()) << s2.getJson(JsonOptions::none);
            assert(false);
        }
    }
#endif

    auto result = ctx.preclaimResult;
    if (result == tesSUCCESS)
        result = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    assert(result != temUNKNOWN);

    if (auto stream = ctx.journal.trace())
        stream << "preclaim result: " << transToken(result);

    bool applied = isTesSuccess(result);
    auto fee = ctx.tx.getFieldAmount(sfFee).xrp();

    if (ctx.size() > oversizeMetaDataCap)
        result = tecOVERSIZE;

    if (isTecClaim(result) && (ctx.view().flags() & tapFAIL_HARD))
    {
        // If the tapFAIL_HARD flag is set, a tec result
        // must not do anything

        ctx.discard();
        applied = false;
    }
    else if (
        (result == tecOVERSIZE) || (result == tecKILLED) ||
        (result == tecEXPIRED) ||
        (isTecClaimHardFail(result, ctx.view().flags())))
    {
        JLOG(ctx.journal.trace())
            << "reapplying because of " << transToken(result);

        // FIXME: This mechanism for doing work while returning a `tec` is
        //        awkward and very limiting. A more general purpose approach
        //        should be used, making it possible to do more useful work
        //        when transactions fail with a `tec` code.
        std::vector<uint256> removedOffers;

        if ((result == tecOVERSIZE) || (result == tecKILLED))
        {
            ctx.visit([&removedOffers](
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
            ctx.visit([&expiredNFTokenOffers](
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
            removeUnfundedOffers2(
                ctx.view(), removedOffers, ctx.app.journal("View"));

        if (result == tecEXPIRED)
            removeExpiredNFTokenOffers2(
                ctx.view(), expiredNFTokenOffers, ctx.app.journal("View"));

        applied = isTecClaim(result);
    }

    if (applied)
    {
        // Check invariants: if `tecINVARIANT_FAILED` is not returned, we can
        // proceed to apply the tx
        result = ctx.checkInvariants(result, fee);

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
                result = ctx.checkInvariants(result, fee);
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
            Throw<std::logic_error>("fee charged is negative!");

        // Charge whatever fee they specified. The fee has already been
        // deducted from the balance of the account that issued the
        // transaction. We just need to account for it in the ledger
        // header.
        if (!ctx.view().open() && fee != beast::zero)
            ctx.destroyXRP(fee);

        // Once we call apply, we will no longer be able to look at ctx.view()
        ctx.apply(result);
    }

    JLOG(ctx.journal.trace())
        << (applied ? "applied " : "not applied ") << transToken(result);

    return {result, applied};
}

}  // namespace ripple
