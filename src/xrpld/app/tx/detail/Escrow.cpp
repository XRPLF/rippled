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

#include <xrpld/app/tx/detail/Escrow.h>

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/conditions/Condition.h>
#include <xrpld/conditions/Fulfillment.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
#define SF_CF_INVALID SF_PRIVATE5
#define SF_CF_VALID SF_PRIVATE6

namespace ripple {

/*
    Escrow
    ======

    Escrow is a feature of the XRP Ledger that allows you to send conditional
    XRP payments. These conditional payments, called escrows, set aside XRP and
    deliver it later when certain conditions are met. Conditions to successfully
    finish an escrow include time-based unlocks and crypto-conditions. Escrows
    can also be set to expire if not finished in time.

    The XRP set aside in an escrow is locked up. No one can use or destroy the
    XRP until the escrow has been successfully finished or canceled. Before the
    expiration time, only the intended receiver can get the XRP. After the
    expiration time, the XRP can only be returned to the sender.

    For more details on escrow, including examples, diagrams and more please
    visit https://xrpl.org/escrow.html

    For details on specific transactions, including fields and validation rules
    please see:

    `EscrowCreate`
    --------------
        See: https://xrpl.org/escrowcreate.html

    `EscrowFinish`
    --------------
        See: https://xrpl.org/escrowfinish.html

    `EscrowCancel`
    --------------
        See: https://xrpl.org/escrowcancel.html
*/

//------------------------------------------------------------------------------

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
static inline bool
after(NetClock::time_point now, std::uint32_t mark)
{
    return now.time_since_epoch().count() > mark;
}

TxConsequences
EscrowCreate::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{
        ctx.tx, isXRP(ctx.tx[sfAmount]) ? ctx.tx[sfAmount].xrp() : beast::zero};
}

NotTEC
EscrowCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    STAmount const amount{ctx.tx[sfAmount]};
    if (!isXRP(amount))
    {
        if (!ctx.rules.enabled(featurePaychanAndEscrowForTokens))
            return temBAD_AMOUNT;

        if (!isLegalNet(amount))
            return temBAD_AMOUNT;

        if (isFakeXRP(amount))
            return temBAD_CURRENCY;

        if (ctx.tx[sfAccount] == amount.getIssuer())
        {
            JLOG(ctx.j.trace())
                << "Malformed transaction: Cannot escrow own tokens to self.";
            return temDST_IS_SRC;
        }
    }

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    // We must specify at least one timeout value
    if (!ctx.tx[~sfCancelAfter] && !ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    // If both finish and cancel times are specified then the cancel time must
    // be strictly after the finish time.
    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
        ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    if (ctx.rules.enabled(fix1571))
    {
        // In the absence of a FinishAfter, the escrow can be finished
        // immediately, which can be confusing. When creating an escrow,
        // we want to ensure that either a FinishAfter time is explicitly
        // specified or a completion condition is attached.
        if (!ctx.tx[~sfFinishAfter] && !ctx.tx[~sfCondition])
            return temMALFORMED;
    }

    if (auto const cb = ctx.tx[~sfCondition])
    {
        using namespace ripple::cryptoconditions;

        std::error_code ec;

        auto condition = Condition::deserialize(*cb, ec);
        if (!condition)
        {
            JLOG(ctx.j.debug())
                << "Malformed condition during escrow creation: "
                << ec.message();
            return temMALFORMED;
        }

        // Conditions other than PrefixSha256 require the
        // "CryptoConditionsSuite" amendment:
        if (condition->type != Type::preimageSha256 &&
            !ctx.rules.enabled(featureCryptoConditionsSuite))
            return temDISABLED;
    }

    return preflight2(ctx);
}

TER
EscrowCreate::preclaim(PreclaimContext const& ctx)
{
    auto const sled = ctx.view.read(keylet::account(ctx.tx[sfDestination]));
    if (!sled)
        return tecNO_DST;
    if (sled->isFieldPresent(sfAMMID))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
EscrowCreate::doApply()
{
    auto const closeTime = ctx_.view().info().parentCloseTime;

    // Prior to fix1571, the cancel and finish times could be greater
    // than or equal to the parent ledgers' close time.
    //
    // With fix1571, we require that they both be strictly greater
    // than the parent ledgers' close time.
    if (ctx_.view().rules().enabled(fix1571))
    {
        if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
            return tecNO_PERMISSION;

        if (ctx_.tx[~sfFinishAfter] && after(closeTime, ctx_.tx[sfFinishAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
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
    }

    auto const account = ctx_.tx[sfAccount];
    auto const sle = ctx_.view().peek(keylet::account(account));
    if (!sle)
        return tefINTERNAL;

    STAmount const amount{ctx_.tx[sfAmount]};

    std::shared_ptr<SLE> sleLine;

    auto const balance = STAmount((*sle)[sfBalance]).xrp();
    auto const reserve =
        ctx_.view().fees().accountReserve((*sle)[sfOwnerCount] + 1);

    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    // Check reserve and funds availability
    if (isXRP(amount))
    {
        if (balance < reserve + STAmount(ctx_.tx[sfAmount]).xrp())
            return tecUNFUNDED;
        // pass
    }
    else
    {
        // preflight will prevent this ever firing, included
        // defensively for completeness
        if (!ctx_.view().rules().enabled(featurePaychanAndEscrowForTokens))
            return tefINTERNAL;

        // check if the escrow is capable of being
        // finished before we allow it to be created
        {
            TER result = trustTransferAllowed(
                ctx_.view(),
                {account, ctx_.tx[sfDestination]},
                amount.issue(),
                ctx_.journal);

            JLOG(ctx_.journal.trace())
                << "EscrowCreate::doApply trustTransferAllowed result="
                << result;

            if (!isTesSuccess(result))
                return result;
        }

        // perform the lock as a dry run before
        // we modify anything on-ledger
        sleLine = ctx_.view().peek(
            keylet::line(account, amount.getIssuer(), amount.getCurrency()));
        if (!sleLine)
        {
            JLOG(ctx_.journal.trace())
                << "EscrowCreate::doApply trustAdjustLockedBalance trustline "
                   "missing "
                << account << "-" << amount.getIssuer() << "/"
                << amount.getCurrency();
            return tecUNFUNDED;
        }

        {
            TER result = trustAdjustLockedBalance(
                ctx_.view(), sleLine, amount, 1, ctx_.journal, DryRun);

            JLOG(ctx_.journal.trace())
                << "EscrowCreate::doApply trustAdjustLockedBalance (dry) "
                   "result="
                << result;

            if (!isTesSuccess(result))
                return result;
        }
    }

    // Check destination account
    {
        auto const sled =
            ctx_.view().read(keylet::account(ctx_.tx[sfDestination]));
        if (!sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
            !ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Obeying the lsfDissalowXRP flag was a bug.  Piggyback on
        // featureDepositAuth to remove the bug.
        if (!ctx_.view().rules().enabled(featureDepositAuth) &&
            ((*sled)[sfFlags] & lsfDisallowXRP))
            return tecNO_TARGET;
    }

    // Create escrow in ledger.  Note that we we use the value from the
    // sequence or ticket.  For more explanation see comments in SeqProxy.h.

    Keylet const escrowKeylet = keylet::escrow(account, seqID(ctx_));

    auto const slep = std::make_shared<SLE>(escrowKeylet);
    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    (*slep)[sfAccount] = account;
    (*slep)[~sfCondition] = ctx_.tx[~sfCondition];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx_.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx_.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    ctx_.view().insert(slep);

    // Add escrow to sender's owner directory
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(account), escrowKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfOwnerNode] = *page;
    }

    // If it's not a self-send, add escrow to recipient's owner directory.
    if (auto const dest = ctx_.tx[sfDestination]; dest != ctx_.tx[sfAccount])
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(dest), escrowKeylet, describeOwnerDir(dest));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfDestinationNode] = *page;
    }

    // Deduct owner's balance, increment owner count
    if (isXRP(amount))
        (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    else
    {
        if (!ctx_.view().rules().enabled(featurePaychanAndEscrowForTokens) ||
            !sleLine)
            return tefINTERNAL;

        // do the lock-up for real now
        TER result = trustAdjustLockedBalance(
            ctx_.view(), sleLine, amount, 1, ctx_.journal, WetRun);

        JLOG(ctx_.journal.trace())
            << "EscrowCreate::doApply trustAdjustLockedBalance (wet) result="
            << result;

        if (!isTesSuccess(result))
            return result;
    }

    adjustOwnerCount(ctx_.view(), sle, 1, ctx_.journal);
    ctx_.view().update(sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static bool
checkCondition(Slice f, Slice c)
{
    using namespace ripple::cryptoconditions;

    std::error_code ec;

    auto condition = Condition::deserialize(c, ec);
    if (!condition)
        return false;

    auto fulfillment = Fulfillment::deserialize(f, ec);
    if (!fulfillment)
        return false;

    return validate(*fulfillment, *condition);
}

NotTEC
EscrowFinish::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
        return temMALFORMED;

    // Verify the transaction signature. If it doesn't work
    // then don't do any more work.
    {
        auto const ret = preflight2(ctx);
        if (!isTesSuccess(ret))
            return ret;
    }

    if (cb && fb)
    {
        auto& router = ctx.app.getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags(id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition(*fb, *cb))
                router.setFlags(id, SF_CF_VALID);
            else
                router.setFlags(id, SF_CF_INVALID);
        }
    }

    if (auto const err = credentials::checkFields(ctx); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
EscrowFinish::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};

    if (auto const fb = tx[~sfFulfillment])
    {
        extraFee += view.fees().base * (32 + (fb->size() / 16));
    }

    return Transactor::calculateBaseFee(view, tx) + extraFee;
}

TER
EscrowFinish::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureCredentials))
        return Transactor::preclaim(ctx);

    if (auto const err = credentials::valid(ctx, ctx.tx[sfAccount]);
        !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
EscrowFinish::doApply()
{
    bool hooksEnabled = view().rules().enabled(featureHooks);

    if (!hooksEnabled && ctx_.tx.isFieldPresent(sfEscrowID))
        return temDISABLED;

    std::optional<uint256> escrowID = ctx_.tx[~sfEscrowID];

    if (escrowID && ctx_.tx[sfOfferSequence] != 0)
        return temMALFORMED;

    Keylet k = escrowID
        ? Keylet(ltESCROW, *escrowID)
        : keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);

    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_TARGET;

    AccountID const account = (*slep)[sfAccount];
    auto const sle = ctx_.view().peek(keylet::account(account));
    auto amount = slep->getFieldAmount(sfAmount);

    // If a cancel time is present, a finish operation should only succeed prior
    // to that time. fix1571 corrects a logic error in the check that would make
    // a finish only succeed strictly after the cancel time.
    if (ctx_.view().rules().enabled(fix1571))
    {
        auto const now = ctx_.view().info().parentCloseTime;

        // Too soon: can't execute before the finish time
        if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
            return tecNO_PERMISSION;

        // Too late: can't execute after the cancel time
        if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        // Too soon?
        if ((*slep)[~sfFinishAfter] &&
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfFinishAfter])
            return tecNO_PERMISSION;

        // Too late?
        if ((*slep)[~sfCancelAfter] &&
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfCancelAfter])
            return tecNO_PERMISSION;
    }

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.app.getHashRouter().getFlags(id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && !(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
                flags = SF_CF_VALID;
            else
                flags = SF_CF_INVALID;

            ctx_.app.getHashRouter().setFlags(id, flags);
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

    // NOTE: Escrow payments cannot be used to fund accounts.
    AccountID const destID = (*slep)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (!sled)
        return tecNO_DST;

    if (ctx_.view().rules().enabled(featureDepositAuth))
    {
        if (auto err = verifyDepositPreauth(ctx_, account_, destID, sled);
            !isTesSuccess(err))
            return err;
    }

    if (!isXRP(amount))
    {
        if (!ctx_.view().rules().enabled(featurePaychanAndEscrowForTokens))
            return tefINTERNAL;

        // perform a dry run of the transfer before we
        // change anything on-ledger
        TER result = trustTransferLockedBalance(
            ctx_.view(),
            account_,  // txn signing account
            sle,       // src account
            sled,      // dst account
            amount,    // xfer amount
            -1,
            j_,
            DryRun  // dry run
        );

        JLOG(j_.trace())
            << "EscrowFinish::doApply trustTransferLockedBalance (dry) result="
            << result;

        if (!isTesSuccess(result))
            return result;
    }

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode])
    {
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(destID), *optPage, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
        }
    }

    if (isXRP(amount))
        (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
    else
    {
        // all the significant complexity of checking the validity of this
        // transfer and ensuring the lines exist etc is hidden away in this
        // function, all we need to do is call it and return if unsuccessful.
        TER result = trustTransferLockedBalance(
            ctx_.view(),
            account_,  // txn signing account
            sle,       // src account
            sled,      // dst account
            amount,    // xfer amount
            -1,
            j_,
            WetRun  // wet run;
        );

        JLOG(j_.trace())
            << "EscrowFinish::doApply trustTransferLockedBalance (wet) result="
            << result;

        if (!isTesSuccess(result))
            return result;
    }

    ctx_.view().update(sled);

    // Adjust source owner count
    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
EscrowCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
EscrowCancel::doApply()
{
    bool hooksEnabled = view().rules().enabled(featureHooks);

    if (!hooksEnabled && ctx_.tx.isFieldPresent(sfEscrowID))
        return temDISABLED;

    std::optional<uint256> escrowID = ctx_.tx[~sfEscrowID];

    if (escrowID && ctx_.tx[sfOfferSequence] != 0)
        return temMALFORMED;

    Keylet k = escrowID
        ? Keylet(ltESCROW, *escrowID)
        : keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);

    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_TARGET;

    if (ctx_.view().rules().enabled(fix1571))
    {
        auto const now = ctx_.view().info().parentCloseTime;

        // No cancel time specified: can't execute at all.
        if (!(*slep)[~sfCancelAfter])
            return tecNO_PERMISSION;

        // Too soon: can't execute before the cancel time.
        if (!after(now, (*slep)[sfCancelAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        // Too soon?
        if (!(*slep)[~sfCancelAfter] ||
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfCancelAfter])
            return tecNO_PERMISSION;
    }

    AccountID const account = (*slep)[sfAccount];
    auto const sle = ctx_.view().peek(keylet::account(account));
    auto amount = slep->getFieldAmount(sfAmount);

    std::shared_ptr<SLE> sleLine;

    if (!isXRP(amount))
    {
        if (!ctx_.view().rules().enabled(featurePaychanAndEscrowForTokens))
            return tefINTERNAL;

        sleLine = ctx_.view().peek(
            keylet::line(account, amount.getIssuer(), amount.getCurrency()));

        // dry run before we make any changes to ledger
        if (TER result = trustAdjustLockedBalance(
                ctx_.view(), sleLine, -amount, -1, ctx_.journal, DryRun);
            result != tesSUCCESS)
            return result;
    }

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode]; optPage)
    {
        if (!ctx_.view().dirRemove(
                keylet::ownerDir((*slep)[sfDestination]),
                *optPage,
                k.key,
                true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
        }
    }

    // Transfer amount back to the owner (or unlock it in TL case)
    if (isXRP(amount))
        (*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount];
    else
    {
        if (!ctx_.view().rules().enabled(featurePaychanAndEscrowForTokens))
            return tefINTERNAL;

        // unlock previously locked tokens from source line
        TER result = trustAdjustLockedBalance(
            ctx_.view(), sleLine, -amount, -1, ctx_.journal, WetRun);

        JLOG(ctx_.journal.trace())
            << "EscrowCancel::doApply trustAdjustLockedBalance (wet) result="
            << result;

        if (!isTesSuccess(result))
            return result;
    }

    // Decrement owner count
    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

}  // namespace ripple
