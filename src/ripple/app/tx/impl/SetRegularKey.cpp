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

#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

XRPAmount
SetRegularKey::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const id = tx.getAccountID(sfAccount);
    auto const spk = tx.getSigningPubKey();

    if (publicKeyType(makeSlice(spk)))
    {
        if (calcAccountID(PublicKey(makeSlice(spk))) == id)
        {
            auto const sle = view.read(keylet::account(id));

            if (sle && (!(sle->getFlags() & lsfPasswordSpent)))
            {
                // flag is armed and they signed with the right account
                return XRPAmount{0};
            }
        }
    }

    return Transactor::calculateBaseFee(view, tx);
}

NotTEC
SetRegularKey::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    if (uTxFlags & tfUniversalMask)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    if (ctx.rules.enabled(fixMasterKeyAsRegularKey) &&
        ctx.tx.isFieldPresent(sfRegularKey) &&
        (ctx.tx.getAccountID(sfRegularKey) == ctx.tx.getAccountID(sfAccount)))
    {
        return temBAD_REGKEY;
    }

    return preflight2(ctx);
}

TER
SetRegularKey::doApply()
{
    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;

    if (!minimumFee(ctx_.app, ctx_.baseFee, view().fees(), view().flags()))
        sle->setFlag(lsfPasswordSpent);

    if (ctx_.tx.isFieldPresent(sfRegularKey))
    {
        sle->setAccountID(sfRegularKey, ctx_.tx.getAccountID(sfRegularKey));
    }
    else
    {
        // Account has disabled master key and no multi-signer signer list.
        if (sle->isFlag(lsfDisableMaster) &&
            !view().peek(keylet::signers(account_)))
            return tecNO_ALTERNATIVE_KEY;

        sle->makeFieldAbsent(sfRegularKey);
    }

    return tesSUCCESS;
}

}  // namespace ripple
