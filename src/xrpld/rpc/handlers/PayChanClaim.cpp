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

#include <xrpld/app/main/Application.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace ripple {

// {
//   secret_key: <signing_secret_key>
//   key_type: optional; either ed25519 or secp256k1 (default to secp256k1)
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
// }
Json::Value
doChannelAuthorize(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::make_error(
            rpcNOT_SUPPORTED, "Signing is not supported by this server.");
    }

    auto const& params(context.params);
    for (auto const& p : {jss::channel_id, jss::amount})
        if (!params.isMember(p))
            return RPC::missing_field_error(p);

    // Compatibility if a key type isn't specified. If it is, the
    // keypairForSignature code will validate parameters and return
    // the appropriate error.
    if (!params.isMember(jss::key_type) && !params.isMember(jss::secret))
        return RPC::missing_field_error(jss::secret);

    Json::Value result;
    std::optional<std::pair<PublicKey, SecretKey>> const keyPair =
        RPC::keypairForSignature(params, result, context.apiVersion);

    XRPL_ASSERT(
        keyPair || RPC::contains_error(result),
        "ripple::doChannelAuthorize : valid keyPair or an error");
    if (!keyPair || RPC::contains_error(result))
        return result;

    PublicKey const& pk = keyPair->first;
    SecretKey const& sk = keyPair->second;

    uint256 channelId;
    if (!channelId.parseHex(params[jss::channel_id].asString()))
        return rpcError(rpcCHANNEL_MALFORMED);

    std::optional<std::uint64_t> const optDrops = params[jss::amount].isString()
        ? to_uint64(params[jss::amount].asString())
        : std::nullopt;

    if (!optDrops)
        return rpcError(rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    Serializer msg;
    serializePayChanAuthorization(msg, channelId, XRPAmount(drops));

    try
    {
        auto const buf = sign(pk, sk, msg.slice());
        result[jss::signature] = strHex(buf);
    }
    catch (std::exception const& ex)
    {
        result = RPC::make_error(
            rpcINTERNAL,
            "Exception occurred during signing: " + std::string(ex.what()));
    }
    return result;
}

// {
//   public_key: <public_key>
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
//   signature: signature to verify
// }
Json::Value
doChannelVerify(RPC::JsonContext& context)
{
    auto const& params(context.params);
    for (auto const& p :
         {jss::public_key, jss::channel_id, jss::amount, jss::signature})
        if (!params.isMember(p))
            return RPC::missing_field_error(p);

    std::optional<PublicKey> pk;
    {
        std::string const strPk = params[jss::public_key].asString();
        pk = parseBase58<PublicKey>(TokenType::AccountPublic, strPk);

        if (!pk)
        {
            auto pkHex = strUnHex(strPk);
            if (!pkHex)
                return rpcError(rpcPUBLIC_MALFORMED);
            auto const pkType = publicKeyType(makeSlice(*pkHex));
            if (!pkType)
                return rpcError(rpcPUBLIC_MALFORMED);
            pk.emplace(makeSlice(*pkHex));
        }
    }

    uint256 channelId;
    if (!channelId.parseHex(params[jss::channel_id].asString()))
        return rpcError(rpcCHANNEL_MALFORMED);

    std::optional<std::uint64_t> const optDrops = params[jss::amount].isString()
        ? to_uint64(params[jss::amount].asString())
        : std::nullopt;

    if (!optDrops)
        return rpcError(rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    auto sig = strUnHex(params[jss::signature].asString());
    if (!sig || !sig->size())
        return rpcError(rpcINVALID_PARAMS);

    Serializer msg;
    serializePayChanAuthorization(msg, channelId, XRPAmount(drops));

    Json::Value result;
    result[jss::signature_verified] =
        verify(*pk, msg.slice(), makeSlice(*sig), /*canonical*/ true);
    return result;
}

}  // namespace ripple
