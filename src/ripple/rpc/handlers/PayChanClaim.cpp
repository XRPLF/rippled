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

#include <ripple/app/main/Application.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

#include <boost/optional.hpp>

namespace ripple {

// {
//   secret_key: <signing_secret_key>
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
// }
Json::Value doChannelAuthorize (RPC::Context& context)
{
    auto const& params (context.params);
    for (auto const& p : {jss::secret, jss::channel_id, jss::amount})
        if (!params.isMember (p))
            return RPC::missing_field_error (p);

    Json::Value result;
    auto const [pk, sk] = RPC::keypairForSignature (params, result);
    if (RPC::contains_error (result))
        return result;

    uint256 channelId;
    if (!channelId.SetHexExact (params[jss::channel_id].asString ()))
        return rpcError (rpcCHANNEL_MALFORMED);

    boost::optional<std::uint64_t> const optDrops =
        params[jss::amount].isString()
        ? to_uint64(params[jss::amount].asString())
        : boost::none;

    if (!optDrops)
        return rpcError (rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    Serializer msg;
    serializePayChanAuthorization (msg, channelId, XRPAmount (drops));

    try
    {
        auto const buf = sign (pk, sk, msg.slice ());
        result[jss::signature] = strHex (buf);
    }
    catch (std::exception&)
    {
        result =
            RPC::make_error (rpcINTERNAL, "Exception occurred during signing.");
    }
    return result;
}

// {
//   public_key: <public_key>
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
//   signature: signature to verify
// }
Json::Value doChannelVerify (RPC::Context& context)
{
    auto const& params (context.params);
    for (auto const& p :
         {jss::public_key, jss::channel_id, jss::amount, jss::signature})
        if (!params.isMember (p))
            return RPC::missing_field_error (p);

    boost::optional<PublicKey> pk;
    {
        std::string const strPk = params[jss::public_key].asString();
        pk = parseBase58<PublicKey>(TokenType::AccountPublic, strPk);

        if (!pk)
        {
            auto pkHex = strUnHex (strPk);
            if (!pkHex)
                return rpcError(rpcPUBLIC_MALFORMED);
            auto const pkType = publicKeyType(makeSlice(*pkHex));
            if (!pkType)
                return rpcError(rpcPUBLIC_MALFORMED);
            pk.emplace(makeSlice(*pkHex));
        }
    }

    uint256 channelId;
    if (!channelId.SetHexExact (params[jss::channel_id].asString ()))
        return rpcError (rpcCHANNEL_MALFORMED);

    boost::optional<std::uint64_t> const optDrops =
        params[jss::amount].isString()
        ? to_uint64(params[jss::amount].asString())
        : boost::none;

    if (!optDrops)
        return rpcError (rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    auto sig = strUnHex (params[jss::signature].asString ());
    if (!sig || !sig->size ())
        return rpcError (rpcINVALID_PARAMS);

    Serializer msg;
    serializePayChanAuthorization (msg, channelId, XRPAmount (drops));

    Json::Value result;
    result[jss::signature_verified] =
        verify (*pk, msg.slice (), makeSlice (*sig), /*canonical*/ true);
    return result;
}

} // ripple
