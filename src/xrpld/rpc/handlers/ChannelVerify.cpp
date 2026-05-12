#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

// {
//   public_key: <public_key>
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
//   signature: signature to verify
// }
json::Value
doChannelVerify(RPC::JsonContext& context)
{
    auto const& params(context.params);
    for (auto const& p : {jss::public_key, jss::channel_id, jss::amount, jss::signature})
    {
        if (!params.isMember(p))
            return RPC::missingFieldError(p);
    }

    std::optional<PublicKey> pk;
    {
        std::string const strPk = params[jss::public_key].asString();
        pk = parseBase58<PublicKey>(TokenType::AccountPublic, strPk);

        if (!pk)
        {
            auto pkHex = strUnHex(strPk);
            if (!pkHex)
                return rpcError(RpcPublicMalformed);
            auto const pkType = publicKeyType(makeSlice(*pkHex));
            if (!pkType)
                return rpcError(RpcPublicMalformed);
            pk.emplace(makeSlice(*pkHex));
        }
    }

    uint256 channelId;
    if (!channelId.parseHex(params[jss::channel_id].asString()))
        return rpcError(RpcChannelMalformed);

    std::optional<std::uint64_t> const optDrops =
        params[jss::amount].isString() ? toUInt64(params[jss::amount].asString()) : std::nullopt;

    if (!optDrops)
        return rpcError(RpcChannelAmtMalformed);

    std::uint64_t const drops = *optDrops;

    auto sig = strUnHex(params[jss::signature].asString());
    if (!sig || sig->empty())
        return rpcError(RpcInvalidParams);

    Serializer msg;
    serializePayChanAuthorization(msg, channelId, XRPAmount(drops));

    json::Value result;
    result[jss::signature_verified] = verify(*pk, msg.slice(), makeSlice(*sig));
    return result;
}

}  // namespace xrpl
