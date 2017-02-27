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
#include <ripple/app/tx/impl/SusPay.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/ledger/View.h>

// During a SusPayFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
#define SF_CF_INVALID  SF_PRIVATE5
#define SF_CF_VALID    SF_PRIVATE6

namespace ripple {

/*
    SuspendedPayment

        A suspended payment ("SusPay") sequesters XRP in its
        own ledger entry until a SusPayFinish or a SusPayCancel
        transaction mentioning the ledger entry is successfully
        applied to the ledger. If the SusPayFinish succeeds,
        the destination account (which must exist) receives the
        XRP. If the SusPayCancel succeeds, the account which
        created the SusPay is credited the XRP.

    SusPayCreate

        When the SusPay is created, an optional condition may
        be attached. The condition is specified by providing
        the cryptographic digest of the condition to be met.

        At the time of creation, one or both of the fields
        sfCancelAfter and sfFinishAfter may be provided. If
        neither field is specified, the transaction is
        malformed.

        Since the SusPay eventually becomes a payment, an
        optional DestinationTag and an optional SourceTag
        is supported in the SusPayCreate transaction.

        Validation rules:

            sfCondition
                If present, specifies a condition, and the
                condition along with its matching fulfillment
                is required on a SusPayFinish.

            sfCancelAfter
                If present, SusPay may be canceled after the
                specified time (seconds after the Ripple epoch).

            sfFinishAfter
                If present, must be prior to sfCancelAfter.
                A SusPayFinish succeeds only in ledgers after
                sfFinishAfter but before sfCancelAfter.

                If absent, same as parentCloseTime

            Malformed if both sfCancelAfter, sfFinishAfter
                are absent.

            Malformed if both sfFinishAfter, sfCancelAfter
                specified and sfCancelAfter <= sfFinishAfter

    SusPayFinish

        Any account may submit a SusPayFinish. If the SusPay
        ledger entry specifies a condition, the SusPayFinish
        must provide the same condition and its associated
        fulfillment in the sfFulfillment field, or else the
        SusPayFinish will fail.

        If the SusPay ledger entry specifies sfFinishAfter, the
        transaction will fail if parentCloseTime <= sfFinishAfter.

        SusPayFinish transactions must be submitted before a
        SusPay's sfCancelAfter if present.

        If the SusPay ledger entry specifies sfCancelAfter, the
        transaction will fail if sfCancelAfter <= parentCloseTime.

        NOTE: The reason the condition must be specified again
              is because it must always be possible to verify
              the condition without retrieving the SusPay
              ledger entry.

    SusPayCancel

        Any account may submit a SusPayCancel transaction.

        If the SusPay ledger entry does not specify a
        sfCancelAfter, the cancel transaction will fail.

        If parentCloseTime <= sfCancelAfter, the transaction
        will fail.

        When a SusPay is canceled, the funds are returned to
        the source account.

    By careful selection of fields in each transaction,
    these operations may be achieved:

        * Lock up XRP for a time period
        * Execute a payment conditionally
*/

//------------------------------------------------------------------------------

XRPAmount
SusPayCreate::calculateMaxSpend(STTx const& tx)
{
    return tx[sfAmount].xrp();
}

TER
SusPayCreate::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureSusPay))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (! isXRP(ctx.tx[sfAmount]))
        return temBAD_AMOUNT;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    if (! ctx.tx[~sfCancelAfter] &&
            ! ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
            ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    if (auto const cb = ctx.tx[~sfCondition])
    {
        if (! ctx.rules.enabled(featureConditionalSusPay))
            return temDISABLED;

        using namespace ripple::cryptoconditions;

        // TODO: Remove this try/catch once cryptoconditions
        //       no longer use exceptions.
        try
        {
            auto condition = loadCondition(*cb);

            if (!condition)
                return temMALFORMED;

            {
                // TODO: This is here temporarily to ensure
                //       that the condition given doesn't
                //       contain unnecessary trailing junk.
                //       The new parsing API will simplify
                //       the checking here.

                auto b = to_blob(*condition);
                if (*cb != makeSlice(b))
                    return temMALFORMED;
            }
        }
        catch (...)
        {
            return temMALFORMED;
        }
    }

    return preflight2 (ctx);
}

TER
SusPayCreate::doApply()
{
    auto const closeTime = ctx_.view ().info ().parentCloseTime;

    if (ctx_.tx[~sfCancelAfter])
    {
        auto const cancelAfter = ctx_.tx[sfCancelAfter];

        if (closeTime.time_since_epoch().count() >= cancelAfter)
            return tecNO_PERMISSION;
    }

    if (ctx_.tx[~sfFinishAfter])
    {
        auto const finishAfter = ctx_.tx[sfFinishAfter];

        if (closeTime.time_since_epoch().count() >= finishAfter)
            return tecNO_PERMISSION;
    }

    auto const account = ctx_.tx[sfAccount];
    auto const sle = ctx_.view().peek(
        keylet::account(account));

    // Check reserve and funds availability
    {
        auto const balance = STAmount((*sle)[sfBalance]).xrp();
        auto const reserve = ctx_.view().fees().accountReserve(
            (*sle)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + STAmount(ctx_.tx[sfAmount]).xrp())
            return tecUNFUNDED;
    }

    // Check destination account
    {
        auto const sled = ctx_.view().read(
            keylet::account(ctx_.tx[sfDestination]));
        if (! sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
                ! ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;
        if ((*sled)[sfFlags] & lsfDisallowXRP)
            return tecNO_TARGET;
    }

    // Create SusPay in ledger
    auto const slep = std::make_shared<SLE>(
        keylet::susPay(account, (*sle)[sfSequence] - 1));
    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    (*slep)[sfAccount] = account;
    (*slep)[~sfCondition] = ctx_.tx[~sfCondition];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx_.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx_.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    ctx_.view().insert(slep);

    // Add SusPay to owner directory
    {
        uint64_t page;
        auto result = dirAdd(ctx_.view(), page,
            keylet::ownerDir(account), slep->key(),
            describeOwnerDir(account), ctx_.app.journal ("View"));
        if (! isTesSuccess(result.first))
            return result.first;
        (*slep)[sfOwnerNode] = page;
    }

    // Deduct owner's balance, increment owner count
    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] + 1;
    ctx_.view().update(sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static
bool
checkCondition (Slice f, Slice c)
{
    using namespace ripple::cryptoconditions;

    // TODO: Remove this try/catch once cryptoconditions
    //       no longer use exceptions.
    try
    {
        auto condition = loadCondition(c);

        if (!condition)
            return false;

        auto fulfillment = loadFulfillment(f);

        if (!fulfillment)
            return false;

        {
            // TODO: This is here temporarily to ensure
            //       that the condition & fulfillment
            //       given don't contain unnecessary
            //       trailing junk. The new parsing API
            //       will simplify the checking here.

            auto cb = to_blob(*condition);
            if (c != makeSlice(cb))
                return false;

            auto fb = to_blob(*fulfillment);
            if (f != makeSlice(fb))
                return false;
        }

        return validateTrigger (*fulfillment, *condition);
    }
    catch (...)
    {
        return false;
    }
}

TER
SusPayFinish::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureSusPay))
        return temDISABLED;

    {
        auto const ret = preflight1 (ctx);
        if (!isTesSuccess (ret))
            return ret;
    }

    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    if (cb || fb)
    {
        if (! ctx.rules.enabled(featureConditionalSusPay))
            return temDISABLED;
    }

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
        return temMALFORMED;

    // Verify the transaction signature. If it doesn't work
    // then don't do any more work.
    {
        auto const ret = preflight2 (ctx);
        if (!isTesSuccess (ret))
            return ret;
    }

    if (cb && fb)
    {
        auto& router = ctx.app.getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags (id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition (*fb, *cb))
                router.setFlags (id, SF_CF_VALID);
            else
                router.setFlags (id, SF_CF_INVALID);
        }
    }

    return tesSUCCESS;
}

std::uint64_t
SusPayFinish::calculateBaseFee (PreclaimContext const& ctx)
{
    std::uint64_t extraFee = 0;

    if (auto const fb = ctx.tx[~sfFulfillment])
    {
        extraFee += ctx.view.fees().units *
            (32 + static_cast<std::uint64_t> (fb->size() / 16));
    }

    return Transactor::calculateBaseFee (ctx) + extraFee;
}


TER
SusPayFinish::doApply()
{
    // peek SusPay SLE
    auto const k = keylet::susPay(
        ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (! slep)
        return tecNO_TARGET;

    // Too soon?
    if ((*slep)[~sfFinishAfter] &&
        ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
            (*slep)[sfFinishAfter])
        return tecNO_PERMISSION;

    // Too late?
    if ((*slep)[~sfCancelAfter] &&
        (*slep)[sfCancelAfter] <=
            ctx_.view().info().parentCloseTime.time_since_epoch().count())
        return tecNO_PERMISSION;

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.app.getHashRouter().getFlags (id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && ! (flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition (*fb, *cb))
                flags = SF_CF_VALID;
            else
                flags = SF_CF_INVALID;

            ctx_.app.getHashRouter().setFlags (id, flags);
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (flags & SF_CF_INVALID)
            return tecCRYPTOCONDITION_ERROR;

        // Check against condition in the ledger entry:
        auto const cond = (*slep)[~sfCondition];

        // If a condition wasn't specified during creation,
        // one shouldn't be included now.
        if (!cond && cb)
            return tecCRYPTOCONDITION_ERROR;

        // If a condition was specified during creation of
        // the suspended payment, the identical condition
        // must be presented again. We don't check if the
        // fulfillment matches the condition since we did
        // that in preflight.
        if (cond && (cond != cb))
            return tecCRYPTOCONDITION_ERROR;
    }

    AccountID const account = (*slep)[sfAccount];

    // Remove SusPay from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        TER const ter = dirDelete(ctx_.view(), true,
            page, keylet::ownerDir(account).key,
                k.key, false, page == 0, ctx_.app.journal ("View"));
        if (! isTesSuccess(ter))
            return ter;
    }

    // NOTE: These payments cannot be used to fund accounts

    // Fetch Destination SLE
    auto const sled = ctx_.view().peek(
        keylet::account((*slep)[sfDestination]));
    if (! sled)
        return tecNO_DST;

    // Transfer amount to destination
    (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
    ctx_.view().update(sled);

    // Adjust source owner count
    auto const sle = ctx_.view().peek(
        keylet::account(account));
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] - 1;
    ctx_.view().update(sle);

    // Remove SusPay from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
SusPayCancel::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureSusPay))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    return preflight2 (ctx);
}

TER
SusPayCancel::doApply()
{
    // peek SusPay SLE
    auto const k = keylet::susPay(
        ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (! slep)
        return tecNO_TARGET;

    // Too soon?
    if (! (*slep)[~sfCancelAfter] ||
        ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
            (*slep)[sfCancelAfter])
        return tecNO_PERMISSION;

    AccountID const account = (*slep)[sfAccount];

    // Remove SusPay from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        TER const ter = dirDelete(ctx_.view(), true,
            page, keylet::ownerDir(account).key,
                k.key, false, page == 0, ctx_.app.journal ("View"));
        if (! isTesSuccess(ter))
            return ter;
    }

    // Transfer amount back to owner, decrement owner count
    auto const sle = ctx_.view().peek(
        keylet::account(account));
    (*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount];
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] - 1;
    ctx_.view().update(sle);

    // Remove SusPay from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

} // ripple

