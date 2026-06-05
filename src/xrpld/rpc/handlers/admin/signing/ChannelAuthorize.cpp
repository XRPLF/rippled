#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

namespace xrpl {

// {
//   secret_key: <signing_secret_key>
//   key_type: optional; either ed25519 or secp256k1 (default to secp256k1)
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
// }
json::Value
doChannelAuthorize(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::makeError(RpcNotSupported, "Signing is not supported by this server.");
    }

    auto const& params(context.params);
    for (auto const& p : {jss::channel_id, jss::amount})
    {
        if (!params.isMember(p))
            return RPC::missingFieldError(p);
    }

    // Compatibility if a key type isn't specified. If it is, the
    // keypairForSignature code will validate parameters and return
    // the appropriate error.
    if (!params.isMember(jss::key_type) && !params.isMember(jss::secret))
        return RPC::missingFieldError(jss::secret);

    json::Value result;
    std::optional<std::pair<PublicKey, SecretKey>> const keyPair =
        RPC::keypairForSignature(params, result, context.apiVersion);

    XRPL_ASSERT(
        keyPair || RPC::containsError(result),
        "xrpl::doChannelAuthorize : valid keyPair or an error");
    if (!keyPair || RPC::containsError(result))
        return result;

    PublicKey const& pk = keyPair->first;
    SecretKey const& sk = keyPair->second;

    uint256 channelId;
    if (!channelId.parseHex(params[jss::channel_id].asString()))
        return rpcError(RpcChannelMalformed);

    std::optional<std::uint64_t> const optDrops =
        params[jss::amount].isString() ? toUint64(params[jss::amount].asString()) : std::nullopt;

    if (!optDrops)
        return rpcError(RpcChannelAmtMalformed);

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
        // LCOV_EXCL_START
        result = RPC::makeError(
            RpcInternal, "Exception occurred during signing: " + std::string(ex.what()));
        // LCOV_EXCL_STOP
    }
    return result;
}

}  // namespace xrpl
