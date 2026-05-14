// Copyright (c) 2019 Dev Null Productions

/** @file
 *  Admin RPC handler that reports the local node's own validator key
 *  configuration.
 *
 *  Validators use a two-tier key scheme: a **master key** kept offline that
 *  signs a **manifest** (a certificate binding the master key to the current
 *  ephemeral signing key), and an **ephemeral key** installed on the live
 *  node that signs ledger validations. `doValidatorInfo` reads the node's
 *  ephemeral key from config, resolves the master key through the manifest
 *  cache, and returns the full identity bundle to the operator. It takes no
 *  parameters and always describes the local node's own state.
 *
 *  @see src/xrpld/rpc/handlers/admin/status/Validators.cpp for the
 *      complementary handler reporting the set of trusted validators from
 *      the UNL.
 *  @see src/xrpld/rpc/handlers/server_info/Manifest.cpp for the public-
 *      facing handler that looks up manifests for arbitrary validators by
 *      submitted public key.
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

/** Report the local node's validator identity: master key, ephemeral key,
 *  active manifest, manifest sequence number, and claimed domain.
 *
 *  The node's ephemeral signing key is read from `[validator_token]` config
 *  and passed to `ManifestCache::getMasterKey()`. If `getMasterKey` returns
 *  the key unchanged (its identity-fallback behaviour when no manifest entry
 *  exists), the node is either unconfigured for two-tier key rotation or is
 *  using its master key directly for signing; in this case only `master_key`
 *  is returned. When the two keys differ, `ephemeral_key`, `manifest`
 *  (base64-encoded binary), `seq`, and `domain` are also included when
 *  present in the manifest cache.
 *
 *  Both keys are encoded as `n…`-prefixed base58check strings
 *  (`TokenType::NodePublic`). The manifest blob is base64 because it is a
 *  binary-serialized XRPL `STObject`.
 *
 *  @param context The RPC dispatch context; must carry admin authorization.
 *      The local node's validation key and manifest cache are accessed via
 *      `context.app`.
 *  @return A JSON object containing `master_key` and, when an active manifest
 *      exists, `ephemeral_key`, `manifest`, `seq`, and (optionally) `domain`.
 *      Returns a `notValidatorError` JSON error object if no validator key is
 *      configured.
 *  @note `manifest`, `seq`, and `domain` are each independently guarded by
 *      an optional check, so any field may be absent without indicating an
 *      error — this defends against transient cache states.
 */
json::Value
doValidatorInfo(RPC::JsonContext& context)
{
    auto const validationPK = context.app.getValidationPublicKey();
    if (!validationPK)
        return RPC::notValidatorError();

    json::Value ret;

    auto const mk = context.app.getValidatorManifests().getMasterKey(*validationPK);
    ret[jss::master_key] = toBase58(TokenType::NodePublic, mk);

    // When getMasterKey returns the input key unchanged, no manifest exists
    // for this node: there is no distinct ephemeral key to report.
    if (mk == validationPK)
        return ret;

    ret[jss::ephemeral_key] = toBase58(TokenType::NodePublic, *validationPK);

    if (auto const manifest = context.app.getValidatorManifests().getManifest(mk))
        ret[jss::manifest] = base64Encode(*manifest);

    if (auto const seq = context.app.getValidatorManifests().getSequence(mk))
        ret[jss::seq] = *seq;

    if (auto const domain = context.app.getValidatorManifests().getDomain(mk))
        ret[jss::domain] = *domain;

    return ret;
}
}  // namespace xrpl
