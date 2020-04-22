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
#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/basics/base64.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {
Json::Value
doValidatorInfo(RPC::JsonContext& context)
{
    // return error if not configured as validator
    if (context.app.getValidationPublicKey().empty())
        return RPC::not_validator_error();

    Json::Value ret;

    auto const pk = context.app.getValidationPublicKey();

    // assume pk is ephemeral key, get master key
    auto const mk = context.app.validatorManifests().getMasterKey(pk);
    ret[jss::master_key] = toBase58(TokenType::NodePublic, mk);

    // pk is maskter key, eg no ephemeral key, eg no manifest, just return
    if (mk == pk)
        return ret;

    // lookup ephemeral key
    auto const ek = context.app.validatorManifests().getSigningKey(mk);
    ret[jss::ephemeral_key] = toBase58(TokenType::NodePublic, ek);

    if (auto const manifest = context.app.validatorManifests().getManifest(mk))
        ret[jss::manifest] = base64_encode(*manifest);

    if (auto const seq = context.app.validatorManifests().getSequence(mk))
        ret[jss::seq] = *seq;

    if (auto const domain = context.app.validatorManifests().getDomain(mk))
        ret[jss::domain] = *domain;

    return ret;
}
}  // namespace ripple
