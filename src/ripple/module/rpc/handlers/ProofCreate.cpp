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

#include <ripple/module/app/misc/ProofOfWorkFactory.h>

namespace ripple {

// {
//   // if either of these parameters is set, a custom generator is used
//   difficulty: <number>       // optional
//   secret: <secret>           // optional
// }
Json::Value doProofCreate (RPC::Context& context)
{
    context.lock_.unlock ();
    // XXX: Add ability to create proof with arbitrary time

    Json::Value     jvResult (Json::objectValue);

    if (context.params_.isMember ("difficulty") || context.params_.isMember ("secret"))
    {
        // VFALCO TODO why aren't we using the app's factory?
        std::unique_ptr <ProofOfWorkFactory> pgGen (ProofOfWorkFactory::New ());

        if (context.params_.isMember ("difficulty"))
        {
            if (!context.params_["difficulty"].isIntegral ())
                return RPC::invalid_field_error ("difficulty");

            int const iDifficulty (context.params_["difficulty"].asInt ());

            if (iDifficulty < 0 || iDifficulty > ProofOfWorkFactory::kMaxDifficulty)
                return RPC::invalid_field_error ("difficulty");

            pgGen->setDifficulty (iDifficulty);
        }

        if (context.params_.isMember ("secret"))
        {
            uint256     uSecret (context.params_["secret"].asString ());
            pgGen->setSecret (uSecret);
        }

        jvResult["token"]   = pgGen->getProof ().getToken ();
        jvResult["secret"]  = to_string (pgGen->getSecret ());
    }
    else
    {
        jvResult["token"]   = getApp().getProofOfWorkFactory ().getProof ().getToken ();
    }

    return jvResult;
}

} // ripple
