//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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


namespace ripple {

// {
//   secret: <string>
// }
Json::Value doValidationSeed (RPC::Context& context)
{
    Json::Value obj (Json::objectValue);

    if (!context.params_.isMember ("secret"))
    {
        Log::out() << "Unset validation seed.";

        getConfig ().VALIDATION_SEED.clear ();
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();
    }
    else if (!getConfig ().VALIDATION_SEED.setSeedGeneric (context.params_["secret"].asString ()))
    {
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();

        return rpcError (rpcBAD_SEED);
    }
    else
    {
        getConfig ().VALIDATION_PUB = RippleAddress::createNodePublic (getConfig ().VALIDATION_SEED);
        getConfig ().VALIDATION_PRIV = RippleAddress::createNodePrivate (getConfig ().VALIDATION_SEED);

        obj["validation_public_key"]    = getConfig ().VALIDATION_PUB.humanNodePublic ();
        obj["validation_seed"]          = getConfig ().VALIDATION_SEED.humanSeed ();
        obj["validation_key"]           = getConfig ().VALIDATION_SEED.humanSeed1751 ();
    }

    return obj;
}

} // ripple
