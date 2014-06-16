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

#include <ripple/module/rpc/impl/GetMasterGenerator.h>

namespace ripple {
namespace RPC {

// Look up the master public generator for a regular seed so we may index source accounts ids.
// --> naRegularSeed
// <-- naMasterGenerator
Json::Value getMasterGenerator (
    Ledger::ref lrLedger, const RippleAddress& naRegularSeed,
    RippleAddress& naMasterGenerator, NetworkOPs& netOps)
{
    RippleAddress       na0Public;      // To find the generator's index.
    RippleAddress       na0Private;     // To decrypt the master generator's cipher.
    RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naRegularSeed);

    na0Public.setAccountPublic (naGenerator, 0);
    na0Private.setAccountPrivate (naGenerator, naRegularSeed, 0);

    SLE::pointer        sleGen          = netOps.getGenerator (lrLedger, na0Public.getAccountID ());

    if (!sleGen)
    {
        // No account has been claimed or has had it password set for seed.
        return rpcError (rpcNO_ACCOUNT);
    }

    Blob    vucCipher           = sleGen->getFieldVL (sfGenerator);
    Blob    vucMasterGenerator  = na0Private.accountPrivateDecrypt (na0Public, vucCipher);

    if (vucMasterGenerator.empty ())
    {
        return rpcError (rpcFAIL_GEN_DECRYPT);
    }

    naMasterGenerator.setGenerator (vucMasterGenerator);

    return Json::Value (Json::objectValue);
}

} // RPC
} // ripple
