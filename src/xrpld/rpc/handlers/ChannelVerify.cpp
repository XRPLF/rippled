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

/** Verify a payment channel claim signature (`channel_verify`).
 *
 *  Reconstructs the canonical signing payload for a payment channel
 *  authorization — `HashPrefix::paymentChannelClaim` (4 bytes) + channel ID
 *  (256 bits) + amount in drops (64-bit big-endian) — then checks whether
 *  the supplied signature is valid over that payload under the supplied public
 *  key. The check is purely mathematical: no ledger state is read and no
 *  network calls are made.
 *
 *  This is the read-only counterpart to `doChannelAuthorize`. It carries no
 *  role restriction and is safe to expose on public nodes.
 *
 *  The `public_key` field accepts two encodings:
 *  - Base58-encoded `AccountPublic` token (the `rXXX…`-style XRPL format).
 *  - Raw hex bytes of a secp256k1 or Ed25519 public key. The key type is
 *    inferred from the byte prefix via `publicKeyType()`; an unrecognised
 *    prefix yields `rpcPUBLIC_MALFORMED`.
 *
 *  The `amount` field must be a decimal-integer string to avoid precision
 *  loss inherent in JSON numeric literals at XRP drop magnitudes (up to
 *  10^17).
 *
 *  @param context  RPC dispatch context; only `context.params` is read.
 *  @return A JSON object with a single key `"signature_verified"` set to
 *      `true` or `false`. An invalid signature is a normal outcome and maps
 *      to `false`, never to an error code.
 *
 *  @note Required fields: `public_key`, `channel_id`, `amount`, `signature`.
 *      Missing fields produce `missingFieldError`. Malformed channel ID or
 *      public key produce `rpcCHANNEL_MALFORMED` / `rpcPUBLIC_MALFORMED`;
 *      malformed amount produces `rpcCHANNEL_AMT_MALFORMED`; empty signature
 *      bytes produce `rpcINVALID_PARAMS`.
 *  @see serializePayChanAuthorization, doChannelAuthorize
 */
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
