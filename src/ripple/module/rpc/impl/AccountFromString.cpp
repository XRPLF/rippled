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

#include <ripple/module/rpc/impl/AccountFromString.h>

namespace ripple {
namespace RPC {

// --> strIdent: public key, account ID, or regular seed.
// --> bStrict: Only allow account id or public key.
// <-- bIndex: true if iIndex > 0 and used the index.
Json::Value accountFromString (
    Ledger::ref lrLedger,
    RippleAddress& naAccount,
    bool& bIndex,
    std::string const& strIdent,
    int const iIndex,
    bool const bStrict,
    NetworkOPs& netOps)
{
    RippleAddress   naSeed;

    if (naAccount.setAccountPublic (strIdent) ||
        naAccount.setAccountID (strIdent))
    {
        // Got the account.
        bIndex = false;
        return Json::Value (Json::objectValue);
    }

    if (bStrict)
    {
        auto isBitcoin = naAccount.setAccountID (
            strIdent, Base58::getBitcoinAlphabet ());
        return rpcError (isBitcoin ? rpcACT_BITCOIN : rpcACT_MALFORMED);
    }

    // If we get here, it must be a seed.
    if (!naSeed.setSeedGeneric (strIdent))
        return rpcError (rpcBAD_SEED);

    // We allow the use of the seeds to access #0.
    // This is poor practice and merely for debuging convenience.
    RippleAddress naRegular0Public;
    RippleAddress naRegular0Private;

    auto naGenerator = RippleAddress::createGeneratorPublic (naSeed);

    naRegular0Public.setAccountPublic (naGenerator, 0);
    naRegular0Private.setAccountPrivate (naGenerator, naSeed, 0);

    SLE::pointer sleGen = netOps.getGenerator (
        lrLedger, naRegular0Public.getAccountID ());

    if (sleGen)
    {
        // Found master public key.
        Blob vucCipher = sleGen->getFieldVL (sfGenerator);
        Blob vucMasterGenerator =
                naRegular0Private.accountPrivateDecrypt (
                    naRegular0Public, vucCipher);

        if (vucMasterGenerator.empty ())
            rpcError (rpcNO_GEN_DECRYPT);

        naGenerator.setGenerator (vucMasterGenerator);
    }
    // Otherwise, if we didn't find a generator map, assume it is a master
    // generator.

    bIndex = !iIndex;
    naAccount.setAccountPublic (naGenerator, iIndex);

    return Json::Value (Json::objectValue);
}

} // RPC
} // ripple
