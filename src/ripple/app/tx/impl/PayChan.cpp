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
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/ledger/View.h>

namespace ripple {

/*
    PaymentChannel

        Payment channels permit off-ledger XRP payments flowing between two
        accounts in both directions. A channel sequesters the participants' XRP
        in its own ledger entry. The participants can authorize the counterparty
        to claim up to a given amount by giving the receiver a signed message
        (off-ledger). The recipient can use this signed message to settle the
        channel balance. A participant can authorize a payment amount exceeding
        their channel balance as long as the difference between the two sides'
        authorized payments does not exceed the respective channel balance.
        Payment amounts can be netted by subtracting both by the lower amount
        and incrementing the sequence number on the authorized payments (off-
        ledger).
        Either participant can top off their channel balance as needed. When
        submitting a channel claim, the participant must wait out a delay to
        close the channel to give the counterparty a chance to supply any
        additional claims. Any transaction that touches the channel after the
        expiration time will close the channel. When the channel is closed any
        remaining balance is returned to the owners.

    PaymentChannelCreate

        Create a bidirectional XRP payment channel. The parameters are:
        Destination
            The other channel participant.
        Amount
            The amount of XRP to deposit in the channel from the transaction
            signer immediately.
        PublicKey
            The key that will sign claims against the channel.
        DstPublicKey
            The key that will be used by the other channel participant to sign
            claims against the channel.
        SettleDelay
            The amount of time everyone must wait after a claim for a superior
            claim.
        CancelAfter (optional)
            Do not honor any claims after the `CancelAfter` time if no claims
            were submitted beforehand.

    PaymentChannelFund

        Add additional funds to the payment channel. Either channel participant
        may use this transaction. The parameters are:
        Channel
            The 256-bit ID of the channel.
        Amount
            The amount of XRP to add.

    PaymentChannelClaim

        Place a claim against an existing channel. A successful claim will
        cause the channel to close after the settle delay unless a subsequent
        claim is submitted. The parameters are:
        Channel
            The 256-bit ID of the channel.
        ChannelClaims
            Claim from one or both of the channel participants. Claim parameters
            are:
            Channel
                The 256-bit ID of the channel.
            Amount
                The amount of XRP the signature is for.
            Sequence
                Sequence incremented by channel participants for amount resets
                off ledger
            PublicKey
               The public key that made the signature
            Signature
                Authorization for the amount above. The signature is for the
                following message: CLM\0 prefix followed by a serialized
                ChannelClaim object.
            SourceTag (optional)
                Source tags allow the different accounts inside of a Hosted Wallet
                to be mapped back onto the Ripple ledger. Source tags are similar to
                destination tags but are for the channel owner to identify their own
                transactions.
            DestinationTag (optional)
                Destination tags allow the different accounts inside of a Hosted
                Wallet to be mapped back onto the Ripple ledger. The destination
                tag tells the server to which account in the Hosted Wallet the
                funds are intended to go to. Required if the destination has
                lsfRequireDestTag set.
            InvoiceID (optional)

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
    STArray const& chanMembers (slep->getFieldArray (sfChannelMembers));

    // Remove PayChan from owner directories
    for (auto const member : chanMembers)
    {
        auto const owner = member[sfAccount];
        auto const page = member[sfOwnerNode];
        TER const ter = dirDelete (view, true, page, keylet::ownerDir (owner).key,
            key, false, page == 0, j);
        if (!isTesSuccess (ter))
            return ter;
    }

    auto const sle0 = view.peek (keylet::account (chanMembers[0][sfAccount]));
    auto const sle1 = view.peek (keylet::account (chanMembers[1][sfAccount]));

    (*sle0)[sfOwnerCount] = (*sle0)[sfOwnerCount] - 1;

    // Settle channel claims
    auto diff = chanMembers[0][sfAmount] - chanMembers[1][sfAmount];

    (*sle0)[sfBalance] =
        (*sle0)[sfBalance] + chanMembers[0][sfBalance] - diff;
    (*sle1)[sfBalance] =
        (*sle1)[sfBalance] + chanMembers[1][sfBalance] + diff;

    view.update (sle0);
    view.update (sle1);

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

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination] ||
            ctx.tx[sfPublicKey] == ctx.tx[sfDstPublicKey])
        return temDST_IS_SRC;

    return preflight2 (ctx);
}

TER
PayChanCreate::preclaim(PreclaimContext const &ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const sle = ctx.view.read (keylet::account (account));

    if ((*sle)[sfFlags] & lsfDisallowXRP)
        return tecNO_TARGET;

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

    STArray chanMembers {sfChannelMembers, 2};
    chanMembers.push_back (STObject (sfChannelMember));
    STObject& member1 = chanMembers.back ();
    member1[sfBalance] = ctx_.tx[sfAmount];
    member1[sfAccount] = account;
    member1[sfPublicKey] = ctx_.tx[sfPublicKey];
    member1[sfAmount] = XRPAmount{beast::zero};
    member1[sfSequence] = 0;

    chanMembers.push_back (STObject (sfChannelMember));
    STObject& member2 = chanMembers.back ();
    member2[sfBalance] = XRPAmount{beast::zero};
    member2[sfAccount] = dst;
    member2[sfPublicKey] = ctx_.tx[sfDstPublicKey];
    member2[sfAmount] = XRPAmount{beast::zero};
    member2[sfSequence] = 0;

    // Add PayChan to owner directories
    {
        uint64_t page;
        auto result = dirAdd (ctx_.view (), page, keylet::ownerDir (account),
            slep->key (), describeOwnerDir (account),
            ctx_.app.journal ("View"));
        if (!isTesSuccess (result.first))
            return result.first;
        member1[sfOwnerNode] = page;

        auto const sled = ctx_.view ().peek (keylet::account (dst));
        result = dirAdd (ctx_.view (), page, keylet::ownerDir (dst),
            slep->key (), describeOwnerDir (dst),
            ctx_.app.journal ("View"));
        if (!isTesSuccess (result.first))
            return result.first;
        member2[sfOwnerNode] = page;
        ctx_.view ().update (sled);
    }

    slep->setFieldArray (sfChannelMembers, chanMembers);
    (*slep)[sfSettleDelay] = ctx_.tx[sfSettleDelay];
    (*slep)[~sfExpiration] = ctx_.tx[~sfCancelAfter];

    ctx_.view ().insert (slep);

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

    auto const txAccount = ctx_.tx[sfAccount];
    auto const expiration = (*slep)[~sfExpiration];

    if (expiration)
    {
        auto const closeTime =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count ();
        if (closeTime >= *expiration)
            return closeChannel (
                slep, ctx_.view (), k.key, ctx_.app.journal ("View"));
    }

    auto chanMembers = slep->getFieldArray (sfChannelMembers);

    auto const i = (chanMembers[0].getAccountID (sfAccount) == txAccount)
        ? 0 : 1;

    // only channel members can add funds
    if (i == 1 && chanMembers[1].getAccountID (sfAccount) != txAccount)
        return tecNO_PERMISSION;

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

    chanMembers[i][sfBalance] = chanMembers[i][sfBalance] + ctx_.tx[sfAmount];
    slep->setFieldArray (sfChannelMembers, chanMembers);
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

    auto const& claims = ctx.tx.getFieldArray (sfChannelClaims);

    if (claims.size() == 2)
    {
        if (claims[0][sfPublicKey] == claims[1][sfPublicKey])
            return temMALFORMED;

        if (claims[0][sfSequence] != claims[1][sfSequence])
            return temBAD_SEQUENCE;
    }

    for (auto const& claim : claims)
    {
        if ((claim.getFName() != sfChannelClaim))
            return temMALFORMED;

        auto const amt = claim[sfAmount];
        if (!isXRP (amt) || amt < beast::zero)
            return temBAD_AMOUNT;

        if (claim[sfPayChannel] != ctx.tx[sfPayChannel])
            return temMALFORMED;

        // Check the signature
        PublicKey const pk (claim[sfPublicKey]);
        if (!verify (
                claim, HashPrefix::paymentChannelClaim, pk, /*canonical*/ true))
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
        return tecNO_ENTRY;

    auto chanMembers = slep->getFieldArray (sfChannelMembers);
    auto const& txClaims = ctx_.tx.getFieldArray (sfChannelClaims);
    bool const thirdPartySubmission =
        chanMembers[0].getAccountID (sfAccount) != ctx_.tx[sfAccount] &&
        chanMembers[1].getAccountID (sfAccount) != ctx_.tx[sfAccount];

    auto const curExpiration = (*slep)[~sfExpiration];
    if (curExpiration)
    {
        auto const closeTime =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count ();
        if (closeTime >= *curExpiration)
            return closeChannel (
                slep, ctx_.view (), k.key, ctx_.app.journal ("View"));

        // PayChanClaim without a claim does nothing if
        // the channel has a pending expiration
        if (txClaims.empty ())
            return tecNO_PERMISSION;
    }
    // third party must have claim if the channel has not expired
    else if (thirdPartySubmission && txClaims.empty ())
        return tecNO_PERMISSION;

    bool submittedOwnClaim = false;

    if (! txClaims.empty ())
    {
        std::vector<STAmount> amounts {
            chanMembers[0][sfAmount],
            chanMembers[1][sfAmount]};

        std::vector<std::uint32_t> sequences {
            chanMembers[0][sfSequence],
            chanMembers[1][sfSequence]};

        for (auto const& txClaim : txClaims)
        {
            auto const i =
                txClaim[sfPublicKey] == chanMembers[0][sfPublicKey] ? 0 : 1;

            if (i == 1 && txClaim[sfPublicKey] != chanMembers[1][sfPublicKey])
                return temBAD_SIGNER;

            if (txClaim[sfSequence] < chanMembers[i][sfSequence])
                return tefPAST_SEQ;

            if (txClaim[sfSequence] == chanMembers[i][sfSequence] &&
                    txClaim[sfAmount] <= chanMembers[i][sfAmount])
                return tefPAST_SEQ;

            amounts[i] = txClaim[sfAmount];
            sequences[i] = txClaim[sfSequence];

            if (ctx_.tx[sfAccount] == chanMembers[i].getAccountID (sfAccount))
                submittedOwnClaim = true;
        }

        // Cannot submit single claim with higher sequence
        if (sequences[0] != sequences[1])
            return terPRE_SEQ;

        if (chanMembers[0][sfBalance] < (amounts[0] - amounts[1]) ||
            chanMembers[1][sfBalance] < (amounts[1] - amounts[0]))
            return tecUNFUNDED_PAYMENT;

        for (auto const& i : {0, 1})
        {
            chanMembers[i][sfAmount] = amounts[i];
            chanMembers[i][sfSequence] = sequences[i];
        }

        slep->setFieldArray (sfChannelMembers, chanMembers);
    }

    // Do not start the settle delay if third party submits claim.
    // Do not reset settle delay if there is already an expiration,
    // and a channel member submits only their own claim.
    // This prevents a participant from perpetually keeping the channel open.
    if ((! thirdPartySubmission || curExpiration) &&
        (! curExpiration || ! submittedOwnClaim || txClaims.size() == 2))
    {
        (*slep)[~sfExpiration] =
            ctx_.view ().info ().parentCloseTime.time_since_epoch ().count () +
                (*slep)[sfSettleDelay];
    }

    ctx_.view ().update (slep);

    return tesSUCCESS;
}

} // ripple

