//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Dev Null Productions

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

#include <ripple/app/main/Application.h>
#include <ripple/basics/base64.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {
Json::Value doManifest (RPC::JsonContext& context)
{
    auto& params = context.params;

    if (!params.isMember(jss::public_key))
        return RPC::missing_field_error (jss::public_key);

    auto const requested = params[jss::public_key].asString();

    Json::Value ret;
    ret[jss::requested] = requested;

    auto const pk = parseBase58<PublicKey>(TokenType::NodePublic, requested);
    if (!pk)
    {
        RPC::inject_error(rpcINVALID_PARAMS, ret);
        return ret;
    }

    // first attempt to use params as ephemeral key,
    // if this lookup succeeds master key will be returned,
    // else pk will just be returned and we will assume that
    // is master key anyways
    auto const mk = context.app.validatorManifests().getMasterKey(*pk);

    auto const ek = context.app.validatorManifests().getSigningKey(mk);

    // if ephemeral key not found, we don't have specified manifest
    if (ek == mk)
        return ret;

    if (auto const manifest = context.app.validatorManifests().getManifest(mk))
        ret[jss::manifest] = base64_encode(*manifest);
    Json::Value details;

    details[jss::master_key] = toBase58(TokenType::NodePublic, mk);
    details[jss::ephemeral_key] = toBase58(TokenType::NodePublic, ek);

    if (auto const seq = context.app.validatorManifests().getSequence(mk))
        details[jss::seq] = *seq;

    if (auto const domain = context.app.validatorManifests().getDomain(mk))
        details[jss::domain] = *domain;

    ret[jss::details] = details;
    return ret;
}
} // ripple
