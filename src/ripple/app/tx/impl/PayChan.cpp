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
#include <ripple/app/tx/impl/PayChan.h>

#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/ledger/View.h>

namespace ripple {

/*
    PaymentChannel

        Payment channels permit off-ledger checkpoints of XRP payments flowing
        in a single direction. A channel sequesters the owner's XRP in its own
        ledger entry. The owner can authorize the recipient to claim up to a
        given balance by giving the receiver a signed message (off-ledger). The
        recipient can use this signed message to claim any unpaid balance while
        the channel remains open. The owner can top off the line as needed. If
        the channel has not paid out all its funds, the owner must wait out a
        delay to close the channel to give the recipient a chance to supply any
        claims. The recipient can close the channel at any time. Any transaction
        that touches the channel after the expiration time will close the
        channel. The total amount paid increases monotonically as newer claims
        are issued. When the channel is closed any remaining balance is returned
        to the owner. Channels are intended to permit intermittent off-ledger
        settlement of ILP trust lines as balances get substantial. For
        bidirectional channels, a payment channel can be used in each direction.

    PaymentChannelCreate

        Create a unidirectional channel. The parameters are:
        Destination
            The recipient at the end of the channel.
        Amount
            The amount of XRP to deposit in the channel immediately.
        SettleDelay
            The amount of time everyone but the recipient must wait for a
            superior claim.
        PublicKey
            The key that will sign claims against the channel.
        CancelAfter (optional)
            Any channel transaction that touches this channel after the
            `CancelAfter` time will close it.
        DestinationTag (optional)
            Destination tags allow the different accounts inside of a Hosted
            Wallet to be mapped back onto the Ripple ledger. The destination tag
            tells the server to which account in the Hosted Wallet the funds are
            intended to go to. Required if the destination has lsfRequireDestTag
            set.
        SourceTag (optional)
            Source tags allow the different accounts inside of a Hosted Wallet
            to be mapped back onto the Ripple ledger. Source tags are similar to
            destination tags but are for the channel owner to identify their own
            transactions.

    PaymentChannelFund

        Add additional funds to the payment channel. Only the channel owner may
        use this transaction. The parameters are:
        Channel
            The 256-bit ID of the channel.
        Amount
            The amount of XRP to add.
        Expiration (optional)
            Time the channel closes. The transaction will fail if the expiration
            times does not satisfy the SettleDelay constraints.

    PaymentChannelClaim

        Place a claim against an existing channel. The parameters are:
        Channel
            The 256-bit ID of the channel.
        Balance (optional)
            The total amount of XRP delivered after this claim is processed (optional, not
            needed if just closing).
        Amount (optional)
            The amount of XRP the signature is for (not needed if equal to Balance or just
            closing the line).
        Signature (optional)
            Authorization for the balance above, signed by the owner (optional,
            not needed if closing or owner is performing the transaction). The
            signature if for the following message: CLM\0 followed by the
            256-bit channel ID, and a 64-bit integer drops.
        PublicKey (optional)
           The public key that made the signature (optional, required if a signature
           is present)
        Flags
            tfCloseChannel
                Request that the channel be closed
            tfRenewChannel
                Request that the channel's expiration be reset. Only the owner
                may renew a channel.

*/

//------------------------------------------------------------------------------

static
TER
closeChannel (
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j)
{
    AccountID const src = (*slep)[sfAccount];
    // Remove PayChan from owner directory
    if (!view.dirRemove (
            keylet::ownerDir(src), (*slep)[sfOwnerNode],
            key, true))
        return tefBAD_LEDGER;

    // Transfer amount back to owner, decrement owner count
    auto const sle = view.peek (keylet::account (src));
    assert ((*slep)[sfAmount] >= (*slep)[sfBalance]);
    (*sle)[sfBalance] =
        (*sle)[sfBalance] + (*slep)[sfAmount] - (*slep)[sfBalance];
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] - 1;
    view.update (sle);

    // Remove PayChan from ledger
    view.erase (slep);
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
PayChanCreate::preflight (PreflightContext const& ctx)
{
    if (!ctx.rules.enabled (featurePayChan, ctx.app.config ().features))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (!isXRP (ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::zero))
        return temBAD_AMOUNT;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        return temDST_IS_SRC;

    return preflight2 (ctx);
}

TER
PayChanCreate::preclaim(PreclaimContext const &ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const sle = ctx.view.read (keylet::account (account));

    // Check reserve and funds availability
    {
        auto const balance = (*sle)[sfBalance];
        auto const reserve =
                ctx.view.fees ().accountReserve ((*sle)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx.tx[sfAmount])
            return tecUNFUNDED;
    }

    auto const dst = ctx.tx[sfDestination];

    {
        // Check destination account
        auto const sled = ctx.view.read (keylet::account (dst));
        if (!sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
            !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;
        if ((*sled)[sfFlags] & lsfDisallowXRP)
            return tecNO_TARGET;
    }

    return tesSUCCESS;
}

TER
PayChanCreate::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const sle = ctx_.view ().peek (keylet::account (account));
    auto const dst = ctx_.tx[sfDestination];

    // Create PayChan in ledger
    auto const slep = std::make_shared<SLE> (
        keylet::payChan (account, dst, (*sle)[sfSequence] - 1));
    // Funds held in this channel
    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    // Amount channel has already paid
    (*slep)[sfBalance] = ctx_.tx[sfAmount].zeroed ();
    (*slep)[sfAccount] = account;
    (*slep)[sfDestination] = dst;
    (*slep)[sfSettleDelay] = ctx_.tx[sfSettleDelay];
    (*slep)[sfPublicKey] = ctx_.tx[sfPublicKey];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    ctx_.view ().insert (slep);

    // Add PayChan to owner directory
    {
        // FIXME: SortedOwnerDirectories
        auto const result = ctx_.view ().dirInsert(
            keylet::ownerDir (account), slep->key(),
            true, describeOwnerDir (account));
        if (!result.first)
            return tecDIR_FULL;
        (*slep)[sfOwnerNode] = result.second;
    }

    // Deduct owner's balance, increment owner count
    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] + 1;
    ctx_.view ().update (sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
PayChanFund::preflight (PreflightContext const& ctx)
{
    if (!ctx.rules.enabled (featurePayChan, ctx.app.config ().features))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (!isXRP (ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::zero))
        return temBAD_AMOUNT;

    return preflight2 (ctx);
}

TER
PayChanFund::doApply()
{
    Keylet const k (ltPAYCHAN, ctx_.tx[sfPayChannel]);
    auto const slep = ctx_.view ().peek (k);
    if (!slep)
        return tecNO_ENTRY;

    AccountID const src = (*slep)[sfAccount];
    auto const txAccount = ctx_.tx[sfAccount];
    auto const expiration = (*slep)[~sfExpiration];

    {
        auto const cancelAfter = (*slep)[~sfCancelAfter];
        auto const closeTime =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count ();
        if ((cancelAfter && closeTime >= *cancelAfter) ||
            (expiration && closeTime >= *expiration))
            return closeChannel (
                slep, ctx_.view (), k.key, ctx_.app.journal ("View"));
    }

    if (src != txAccount)
        // only the owner can add funds or extend
        return tecNO_PERMISSION;

    if (auto extend = ctx_.tx[~sfExpiration])
    {
        auto minExpiration =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count () +
                (*slep)[sfSettleDelay];
        if (expiration && *expiration < minExpiration)
            minExpiration = *expiration;

        if (*extend < minExpiration)
            return temBAD_EXPIRATION;
        (*slep)[~sfExpiration] = *extend;
        ctx_.view ().update (slep);
    }

    auto const sle = ctx_.view ().peek (keylet::account (txAccount));

    {
        // Check reserve and funds availability
        auto const balance = (*sle)[sfBalance];
        auto const reserve =
            ctx_.view ().fees ().accountReserve ((*sle)[sfOwnerCount]);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx_.tx[sfAmount])
            return tecUNFUNDED;
    }

    (*slep)[sfAmount] = (*slep)[sfAmount] + ctx_.tx[sfAmount];
    ctx_.view ().update (slep);

    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    ctx_.view ().update (sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
PayChanClaim::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featurePayChan,
            ctx.app.config().features))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto const bal = ctx.tx[~sfBalance];
    if (bal && (!isXRP (*bal) || *bal <= beast::zero))
        return temBAD_AMOUNT;

    auto const amt = ctx.tx[~sfAmount];
    if (amt && (!isXRP (*amt) || *amt <= beast::zero))
        return temBAD_AMOUNT;

    if (bal && amt && *bal > *amt)
        return tecNO_PERMISSION;

    auto const flags = ctx.tx.getFlags ();
    if ((flags & tfClose) && (flags & tfRenew))
        return temMALFORMED;

    if (auto const sig = ctx.tx[~sfSignature])
    {
        if (!(ctx.tx[~sfPublicKey] && bal))
            return temMALFORMED;

        // Check the signature
        // The signature isn't needed if txAccount == src, but if it's
        // present, check it

        auto const reqBalance = bal->xrp ();
        auto const authAmt = amt ? amt->xrp() : reqBalance;

        if (reqBalance > authAmt)
            return tecNO_PERMISSION;

        Keylet const k (ltPAYCHAN, ctx.tx[sfPayChannel]);
        PublicKey const pk (ctx.tx[sfPublicKey]);
        Serializer msg;
        serializePayChanAuthorization (msg, k.key, authAmt);
        if (!verify (pk, msg.slice (), *sig, /*canonical*/ true))
            return temBAD_SIGNATURE;
    }

    return preflight2 (ctx);
}

TER
PayChanClaim::doApply()
{
    Keylet const k (ltPAYCHAN, ctx_.tx[sfPayChannel]);
    auto const slep = ctx_.view ().peek (k);
    if (!slep)
        return tecNO_TARGET;

    AccountID const src = (*slep)[sfAccount];
    AccountID const dst = (*slep)[sfDestination];
    AccountID const txAccount = ctx_.tx[sfAccount];

    auto const curExpiration = (*slep)[~sfExpiration];
    {
        auto const cancelAfter = (*slep)[~sfCancelAfter];
        auto const closeTime =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count ();
        if ((cancelAfter && closeTime >= *cancelAfter) ||
            (curExpiration && closeTime >= *curExpiration))
            return closeChannel (
                slep, ctx_.view (), k.key, ctx_.app.journal ("View"));
    }

    if (txAccount != src && txAccount != dst)
        return tecNO_PERMISSION;

    if (ctx_.tx[~sfBalance])
    {
        auto const chanBalance = slep->getFieldAmount (sfBalance).xrp ();
        auto const chanFunds = slep->getFieldAmount (sfAmount).xrp ();
        auto const reqBalance = ctx_.tx[sfBalance].xrp ();

        if (txAccount == dst && !ctx_.tx[~sfSignature])
            return temBAD_SIGNATURE;

        if (ctx_.tx[~sfSignature])
        {
            PublicKey const pk ((*slep)[sfPublicKey]);
            if (ctx_.tx[sfPublicKey] != pk)
                return temBAD_SIGNER;
        }

        if (reqBalance > chanFunds)
            return tecUNFUNDED_PAYMENT;

        if (reqBalance <= chanBalance)
            // nothing requested
            return tecUNFUNDED_PAYMENT;

        auto const sled = ctx_.view ().peek (keylet::account (dst));
        if (!sled)
            return terNO_ACCOUNT;

        if (txAccount == src && ((*sled)[sfFlags] & lsfDisallowXRP))
            return tecNO_TARGET;

        (*slep)[sfBalance] = ctx_.tx[sfBalance];
        XRPAmount const reqDelta = reqBalance - chanBalance;
        assert (reqDelta >= beast::zero);
        (*sled)[sfBalance] = (*sled)[sfBalance] + reqDelta;
        ctx_.view ().update (sled);
        ctx_.view ().update (slep);
    }

    if (ctx_.tx.getFlags () & tfRenew)
    {
        if (src != txAccount)
            return tecNO_PERMISSION;
        (*slep)[~sfExpiration] = boost::none;
        ctx_.view ().update (slep);
    }

    if (ctx_.tx.getFlags () & tfClose)
    {
        // Channel will close immediately if dry or the receiver closes
        if (dst == txAccount || (*slep)[sfBalance] == (*slep)[sfAmount])
            return closeChannel (
                slep, ctx_.view (), k.key, ctx_.app.journal ("View"));

        auto const settleExpiration =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count () +
                (*slep)[sfSettleDelay];

        if (!curExpiration || *curExpiration > settleExpiration)
        {
            (*slep)[~sfExpiration] = settleExpiration;
            ctx_.view ().update (slep);
        }
    }

    return tesSUCCESS;
}

} // ripple

