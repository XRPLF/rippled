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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

// {
//   tx_json: <object>,
//   secret: <secret key>
// }
Json::Value doChannelAuthorize (RPC::Context& context)
{
    auto const& params (context.params);
    for (auto const& p : {jss::secret, jss::tx_json})
        if (!params.isMember (p))
            return RPC::missing_field_error (p);

    STParsedJSONObject claim ("claim", params[jss::tx_json], sfChannelClaim);

    if (claim.object == boost::none)
        return RPC::make_error (
            rpcINTERNAL, claim.error[jss::error_message].asString ());

    auto const sk = parseBase58<SecretKey>(
        TokenType::TOKEN_ACCOUNT_SECRET, params[jss::secret].asString ());
    auto const pk = get<PublicKey>(*claim.object, sfPublicKey);

    if (! sk || ! pk)
        return rpcError (rpcINVALID_PARAMS);

    auto keyType = publicKeyType (*pk);
    if (! keyType || *pk != derivePublicKey (*keyType, *sk))
        return rpcError (rpcINVALID_PARAMS);

    try
    {
        sign (*claim.object, HashPrefix::paymentChannelClaim, *keyType, *sk);
    }
    catch (std::exception&)
    {
        return RPC::make_error (
            rpcINTERNAL, "Exception occurred during signing.");
    }

    Json::Value result;
    result["ChannelClaim"] = claim.object->getJson (0);
    return result;
}

// {
//   tx_json: <object>
// }
Json::Value doChannelVerify (RPC::Context& context)
{
    auto const& params (context.params);
    if (!params.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    STParsedJSONObject claim ("claim", params[jss::tx_json], sfChannelClaim);

    if (claim.object == boost::none)
        return RPC::make_error (
            rpcINTERNAL, claim.error[jss::error_message].asString ());

    auto const pk = get<PublicKey>(*claim.object, sfPublicKey);

    if (! pk)
        return rpcError (rpcINVALID_PARAMS);

    Json::Value result;
    result[jss::signature_verified] = verify (
        *claim.object, HashPrefix::paymentChannelClaim, *pk, /*canonical*/ true);

    return result;
}

} // ripple
