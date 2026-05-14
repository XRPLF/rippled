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

/** Sign a payment channel authorization (`channel_authorize`).
 *
 *  Produces a cryptographic signature that a channel recipient can present
 *  in a `PaymentChannelClaim` transaction to redeem up to `amount` drops
 *  without requiring the channel owner to be online at redemption time.
 *
 *  The signed payload is constructed by `serializePayChanAuthorization`:
 *  `HashPrefix::paymentChannelClaim` (4-byte domain separator `'CLM\0'`) +
 *  32-byte channel ID + 8-byte drops in big-endian. The domain separator
 *  prevents the signature from being reinterpreted as authorization for any
 *  other XRPL data structure. The resulting binary signature is returned as a
 *  hex string in `"signature"`.
 *
 *  **Access control:** The handler is restricted to `Role::ADMIN` callers
 *  or nodes with `[signing_support]` enabled in their configuration. Public
 *  nodes reject this call by default to avoid exposing key material on
 *  internet-facing endpoints.
 *
 *  **Key material:** Accepts `secret`, `passphrase`, `seed`, or `seed_hex`,
 *  optionally paired with `key_type` (`"secp256k1"` or `"ed25519"`). When
 *  `key_type` is absent and `secret` is also absent, a `missingFieldError`
 *  for `secret` is returned immediately, providing a clear error for legacy
 *  clients that omit both fields. When `key_type` is present, `secret` alone
 *  is rejected — the caller must use one of the explicit seed encodings.
 *
 *  **Amount encoding:** `amount` must be a decimal-integer string (not a JSON
 *  number) to avoid precision loss for values that exceed JavaScript's safe
 *  integer range (~10^17 drops ≈ max XRP supply).
 *
 *  @param context  RPC dispatch context carrying `params`, `role`, and `app`.
 *  @return A JSON object with key `"signature"` containing the hex-encoded
 *      signature, or an error object on any validation failure.
 *
 *  @note Required params: `channel_id` (64-hex-char uint256), `amount`
 *      (decimal string, drops), and at least one of `secret` / `key_type`.
 *      Optional: `key_type` (`"secp256k1"` default, or `"ed25519"`),
 *      `passphrase`, `seed`, `seed_hex`.
 *  @note Error codes: `rpcNOT_SUPPORTED` (signing disabled), `missingField`
 *      (missing required param), `rpcCHANNEL_MALFORMED` (bad channel ID),
 *      `rpcCHANNEL_AMT_MALFORMED` (non-string or unparseable amount),
 *      `rpcINTERNAL` (unexpected exception from `sign()`; unreachable under
 *      normal conditions — excluded from coverage via `LCOV_EXCL`).
 *  @see doChannelVerify, serializePayChanAuthorization,
 *      RPC::keypairForSignature
 */
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

    // Early guard for legacy clients that supply neither field; when key_type
    // is present, keypairForSignature handles the error itself.
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
        params[jss::amount].isString() ? toUInt64(params[jss::amount].asString()) : std::nullopt;

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
