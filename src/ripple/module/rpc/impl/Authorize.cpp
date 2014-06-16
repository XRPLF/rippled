//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/module/rpc/impl/Authorize.h>

namespace ripple {
namespace RPC {

// Given a seed and a source account get the regular public and private key for authorizing transactions.
// - Make sure the source account can pay.
// --> naRegularSeed : To find the generator
// --> naSrcAccountID : Account we want the public and private regular keys to.
// <-- naAccountPublic : Regular public key for naSrcAccountID
// <-- naAccountPrivate : Regular private key for naSrcAccountID
// <-- saSrcBalance: Balance minus fee.
// --> naVerifyGenerator : If provided, the found master public generator must match.
// XXX Be more lenient, allow use of master generator on claimed accounts.
Json::Value authorize (Ledger::ref lrLedger,
                       const RippleAddress& naRegularSeed, const RippleAddress& naSrcAccountID,
                       RippleAddress& naAccountPublic, RippleAddress& naAccountPrivate,
                       STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
                       const RippleAddress& naVerifyGenerator, NetworkOPs& netOps)
{
    // Source/paying account must exist.
    asSrc   = netOps.getAccountState (lrLedger, naSrcAccountID);

    if (!asSrc)
    {
        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    RippleAddress   naMasterGenerator;

    if (asSrc->haveAuthorizedKey ())
    {
        Json::Value obj = getMasterGenerator (lrLedger, naRegularSeed, naMasterGenerator, netOps);

        if (!obj.empty ())
            return obj;
    }
    else
    {
        // Try the seed as a master seed.
        naMasterGenerator   = RippleAddress::createGeneratorPublic (naRegularSeed);
    }

    // If naVerifyGenerator is provided, make sure it is the master generator.
    if (naVerifyGenerator.isValid () && naMasterGenerator != naVerifyGenerator)
    {
        return rpcError (rpcWRONG_SEED);
    }

    // Find the index of the account from the master generator, so we can generate the public and private keys.
    RippleAddress       naMasterAccountPublic;
    unsigned int        iIndex  = 0;
    bool                bFound  = false;

    // Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
    // related.
    while (!bFound && iIndex != getConfig ().ACCOUNT_PROBE_MAX)
    {
        naMasterAccountPublic.setAccountPublic (naMasterGenerator, iIndex);

        WriteLog (lsDEBUG, RPCHandler) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID () << " : " << naSrcAccountID.humanAccountID ();

        bFound  = naSrcAccountID.getAccountID () == naMasterAccountPublic.getAccountID ();

        if (!bFound)
            ++iIndex;
    }

    if (!bFound)
    {
        return rpcError (rpcACT_NOT_FOUND);
    }

    // Use the regular generator to determine the associated public and private keys.
    RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naRegularSeed);

    naAccountPublic.setAccountPublic (naGenerator, iIndex);
    naAccountPrivate.setAccountPrivate (naGenerator, naRegularSeed, iIndex);

    if (asSrc->haveAuthorizedKey () && (asSrc->getAuthorizedKey ().getAccountID () != naAccountPublic.getAccountID ()))
    {
        // Log::out() << "iIndex: " << iIndex;
        // Log::out() << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID());
        // Log::out() << "naAccountPublic: " << strHex(naAccountPublic.getAccountID());

        return rpcError (rpcPASSWD_CHANGED);
    }

    saSrcBalance    = asSrc->getBalance ();

    if (saSrcBalance < saFee)
    {
        WriteLog (lsINFO, RPCHandler) << "authorize: Insufficient funds for fees: fee=" << saFee.getText () << " balance=" << saSrcBalance.getText ();

        return rpcError (rpcINSUF_FUNDS);
    }
    else
    {
        saSrcBalance -= saFee;
    }

    return Json::Value ();
}

} // RPC
} // ripple
