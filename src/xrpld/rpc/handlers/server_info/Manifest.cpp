// Copyright (c) 2019 Dev Null Productions

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base64.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

namespace xrpl {
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

    // first attempt to use params as ephemeral key,
    // if this lookup succeeds master key will be returned,
    // else an unseated optional is returned
    auto const mk = context.app.getValidatorManifests().getMasterKey(*pk);

    auto const ek = context.app.getValidatorManifests().getSigningKey(mk);

    // if ephemeral key not found, we don't have specified manifest
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
