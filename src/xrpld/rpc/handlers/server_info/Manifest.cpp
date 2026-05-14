// Copyright (c) 2019 Dev Null Productions

/** @file
 *  Implements the `manifest` RPC handler, which exposes a read-only view of
 *  the validator manifest cache over RPC.
 *
 *  A manifest is a signed certificate binding a long-lived validator master key
 *  to a rotating ephemeral signing key. Clients can supply either key type;
 *  the handler resolves whichever was provided to the canonical master key
 *  before querying the cache.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base64.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

namespace xrpl {
/** Look up and return the manifest for a validator public key.
 *
 *  Accepts either a master key or an active ephemeral signing key in
 *  `public_key`. The input is resolved to a master key via
 *  `ManifestCache::getMasterKey`, which returns the input unchanged when no
 *  reverse ephemeral→master mapping exists, making the lookup uniform for
 *  both key types.
 *
 *  If the resolved master key has no active signing key in the cache (manifest
 *  absent or revoked), the response contains only the `requested` field and no
 *  error is raised — absence is not treated as a failure.
 *
 *  On success the response contains:
 *  - `requested`      — the caller's original input, always present.
 *  - `manifest`       — Base64-encoded raw manifest bytes, when available.
 *  - `details`        — object with `master_key` and `ephemeral_key` (Base58),
 *                       and optionally `seq` and `domain` when recorded.
 *
 *  @param context  RPC dispatch context carrying `params` and the application
 *      reference used to reach `ManifestCache`.
 *  @return JSON response object. Returns a `missingFieldError` if `public_key`
 *      is absent, `rpcINVALID_PARAMS` if the value is not a valid Base58
 *      NodePublic key, or a sparse response (only `requested`) when the
 *      manifest is not found.
 *  @note All `ManifestCache` methods called here are protected by a
 *      `std::shared_mutex` and are safe for concurrent invocation. This
 *      handler holds no locks and carries no mutable state.
 *  @see doValidatorInfo for the analogous handler that looks up the node's own
 *      configured validation key without accepting external input.
 */
json::Value
doManifest(RPC::JsonContext& context)
{
    auto& params = context.params;

    if (!params.isMember(jss::public_key))
        return RPC::missingFieldError(jss::public_key);

    auto const requested = params[jss::public_key].asString();

    json::Value ret;
    ret[jss::requested] = requested;

    auto const pk = parseBase58<PublicKey>(TokenType::NodePublic, requested);
    if (!pk)
    {
        RPC::injectError(RpcInvalidParams, ret);
        return ret;
    }

    auto const mk = context.app.getValidatorManifests().getMasterKey(*pk);

    auto const ek = context.app.getValidatorManifests().getSigningKey(mk);

    if (!ek)
        return ret;

    if (auto const manifest = context.app.getValidatorManifests().getManifest(mk))
        ret[jss::manifest] = base64Encode(*manifest);
    json::Value details;

    details[jss::master_key] = toBase58(TokenType::NodePublic, mk);
    details[jss::ephemeral_key] = toBase58(TokenType::NodePublic, *ek);

    if (auto const seq = context.app.getValidatorManifests().getSequence(mk))
        details[jss::seq] = *seq;

    if (auto const domain = context.app.getValidatorManifests().getDomain(mk))
        details[jss::domain] = *domain;

    ret[jss::details] = details;
    return ret;
}
}  // namespace xrpl
