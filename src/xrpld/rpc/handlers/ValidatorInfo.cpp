// Copyright (c) 2019 Dev Null Productions

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base64.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
Json::Value
doValidatorInfo(RPC::JsonContext& context)
{
    // return error if not configured as validator
    auto const validationPK = context.app.getValidationPublicKey();
    if (!validationPK)
        return RPC::not_validator_error();

    Json::Value ret;

    // assume validationPK is ephemeral key, get master key
    auto const mk =
        context.app.validatorManifests().getMasterKey(*validationPK);
    ret[jss::master_key] = toBase58(TokenType::NodePublic, mk);

    // validationPK is master key, this implies that there is no ephemeral
    // key, no manifest, hence return
    if (mk == validationPK)
        return ret;

    ret[jss::ephemeral_key] = toBase58(TokenType::NodePublic, *validationPK);

    if (auto const manifest = context.app.validatorManifests().getManifest(mk))
        ret[jss::manifest] = base64_encode(*manifest);

    if (auto const seq = context.app.validatorManifests().getSequence(mk))
        ret[jss::seq] = *seq;

    if (auto const domain = context.app.validatorManifests().getDomain(mk))
        ret[jss::domain] = *domain;

    return ret;
}
}  // namespace ripple
