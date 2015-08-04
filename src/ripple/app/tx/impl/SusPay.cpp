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
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/ledger/View.h>

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

            sfDigest
                If present, proof is required on a SusPayFinish.

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
        must provide the sfMethod, original sfDigest, and
        sfProof fields. Depending on the method, a
        cryptographic operation will be performed on sfProof
        and the result must match the sfDigest or else the
        SusPayFinish is considered as having an invalid
        signature.

        Only sfMethod==1 is supported, where sfProof must be a
        256-bit unsigned big-endian integer which when hashed
        using SHA256 produces digest == sfDigest.

        If the SusPay ledger entry specifies sfFinishAfter, the
        transaction will fail if parentCloseTime <= sfFinishAfter.

        SusPayFinish transactions must be submitted before a
        SusPay's sfCancelAfter if present.

        If the SusPay ledger entry specifies sfCancelAfter, the
        transaction will fail if sfCancelAfter <= parentCloseTime.

        NOTE: It must always be possible to verify the condition
              without retrieving the SusPay ledger entry.

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

TER
SusPayCreate::preflight (PreflightContext const& ctx)
{
    if (! (ctx.flags & tapENABLE_TESTING) &&
        ! ctx.rules.enabled(featureSusPay,
            ctx.app.config().features))
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

    return preflight2 (ctx);
}

TER
SusPayCreate::doApply()
{
    // For now, require that all operations can be
    // canceled, or finished without proof, within a
    // reasonable period of time for the first release.
    using namespace std::chrono;
    auto const maxExpire =
        ctx_.view().info().parentCloseTime +
            seconds{days(7)}.count();
    if (ctx_.tx[~sfDigest])
    {
        if (! ctx_.tx[~sfCancelAfter] ||
                maxExpire <= ctx_.tx[sfCancelAfter])
            return tecNO_PERMISSION;
    }
    else
    {
        if (ctx_.tx[~sfCancelAfter] &&
                maxExpire <= ctx_.tx[sfCancelAfter])
            return tecNO_PERMISSION;
        if (ctx_.tx[~sfFinishAfter] &&
                maxExpire <= ctx_.tx[sfFinishAfter])
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
    (*slep)[~sfDigest] = ctx_.tx[~sfDigest];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx_.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx_.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    ctx_.view().insert(slep);

    // Add SusPay to owner directory
    {
        uint64_t page;
        TER ter = dirAdd(ctx_.view(), page,
            keylet::ownerDir(account).key,
                slep->key(), describeOwnerDir(account), ctx_.app.journal ("View"));
        if (! isTesSuccess(ter))
            return ter;
        (*slep)[sfOwnerNode] = page;
    }

    // Deduct owner's balance, increment owner count
    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] + 1;
    ctx_.view().update(sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
SusPayFinish::preflight (PreflightContext const& ctx)
{
    if (! (ctx.flags & tapENABLE_TESTING) &&
        ! ctx.rules.enabled(featureSusPay,
            ctx.app.config().features))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (ctx.tx[~sfMethod])
    {
        // Condition
        switch(ctx.tx[sfMethod])
        {
        case 1:
        {
            if (! ctx.tx[~sfDigest])
                return temMALFORMED;
            if (! ctx.tx[~sfProof])
                return temMALFORMED;
            if (ctx.tx[~sfProof]->size() != 32)
                return temMALFORMED;
            sha256_hasher h;
            using beast::hash_append;
            hash_append(h, ctx.tx[sfProof]);
            uint256 digest;
            {
                auto const result = static_cast<
                    sha256_hasher::result_type>(h);
                std::memcpy(digest.data(),
                    result.data(), result.size());
            }
            if (digest != ctx.tx[sfDigest])
                return temBAD_SIGNATURE;
            break;
        }
        default:
            return temMALFORMED;
        }
    }
    else
    {
        // No Condition
        if (ctx.tx[~sfDigest])
            return temMALFORMED;
        if (ctx.tx[~sfProof])
            return temMALFORMED;
    }

    return preflight2 (ctx);
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
        ctx_.view().info().parentCloseTime <=
            (*slep)[sfFinishAfter])
        return tecNO_PERMISSION;

    // Too late?
    if ((*slep)[~sfCancelAfter] &&
        (*slep)[sfCancelAfter] <=
            ctx_.view().info().parentCloseTime)
        return tecNO_PERMISSION;

    // Same digest?
    if ((*slep)[~sfDigest] && (! ctx_.tx[~sfMethod] ||
            (ctx_.tx[~sfDigest] != (*slep)[~sfDigest])))
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
    if (! (ctx.flags & tapENABLE_TESTING) &&
        ! ctx.rules.enabled(featureSusPay,
            ctx.app.config().features))
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
        ctx_.view().info().parentCloseTime <=
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

