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
doValidatorInfo(RPC::JsonContext& context)
{
    // return error if not configured as validator
    auto const validationPK = context.app.getValidationPublicKey();
    if (!validationPK)
        return RPC::notValidatorError();

    json::Value ret;

    // assume validationPK is ephemeral key, get master key
    auto const mk = context.app.getValidatorManifests().getMasterKey(*validationPK);
    ret[jss::master_key] = toBase58(TokenType::NodePublic, mk);

    // validationPK is master key, this implies that there is no ephemeral
    // key, no manifest, hence return
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
