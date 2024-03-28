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
#include <ripple/app/tx/impl/PreflightContext.h>
#include <ripple/app/tx/validity.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Protocol.h>

namespace ripple {

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

/** Performs early sanity checks on the txid */
NotTEC
preflight0(PreflightContext const& ctx)
{
    if (!isPseudoTx(ctx.tx) || ctx.tx.isFieldPresent(sfNetworkID))
    {
        uint32_t nodeNID = ctx.app.config().NETWORK_ID;
        std::optional<uint32_t> txNID = ctx.tx[~sfNetworkID];

        if (nodeNID <= 1024)
        {
            // legacy networks have ids less than 1024, these networks cannot
            // specify NetworkID in txn
            if (txNID)
                return telNETWORK_ID_MAKES_TX_NON_CANONICAL;
        }
        else
        {
            // new networks both require the field to be present and require it
            // to match
            if (!txNID)
                return telREQUIRES_NETWORK_ID;

            if (*txNID != nodeNID)
                return telWRONG_NETWORK;
        }
    }

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

}  // namespace ripple
