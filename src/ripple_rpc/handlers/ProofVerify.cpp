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
//   token: <token>
//   solution: <solution>
//   // if either of these parameters is set, a custom verifier is used
//   difficulty: <number>       // optional
//   secret: <secret>           // optional
// }
Json::Value RPCHandler::doProofVerify (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    // XXX Add ability to check proof against arbitrary time

    Json::Value         jvResult;

    if (!params.isMember ("token"))
        return RPC::missing_field_error ("token");

    if (!params.isMember ("solution"))
        return RPC::missing_field_error ("solution");

    std::string     strToken    = params["token"].asString ();
    uint256         uSolution (params["solution"].asString ());

    PowResult       prResult;

    if (params.isMember ("difficulty") || params.isMember ("secret"))
    {
        // VFALCO TODO why aren't we using the app's factory?
        std::unique_ptr <ProofOfWorkFactory> pgGen (ProofOfWorkFactory::New ());

        if (params.isMember ("difficulty"))
        {
            if (!params["difficulty"].isIntegral ())
                return RPC::invalid_field_error ("difficulty");

            int iDifficulty = params["difficulty"].asInt ();

            if (iDifficulty < 0 || iDifficulty > ProofOfWorkFactory::kMaxDifficulty)
                return RPC::missing_field_error ("difficulty");

            pgGen->setDifficulty (iDifficulty);
        }

        if (params.isMember ("secret"))
        {
            uint256     uSecret (params["secret"].asString ());
            pgGen->setSecret (uSecret);
        }

        prResult                = pgGen->checkProof (strToken, uSolution);

        jvResult["secret"]      = to_string (pgGen->getSecret ());
    }
    else
    {
        // XXX Proof should not be marked as used from this
        prResult = getApp().getProofOfWorkFactory ().checkProof (strToken, uSolution);
    }

    std::string sToken;
    std::string sHuman;

    ProofOfWork::calcResultInfo (prResult, sToken, sHuman);

    jvResult["proof_result"]            = sToken;
    jvResult["proof_result_code"]       = prResult;
    jvResult["proof_result_message"]    = sHuman;

    return jvResult;
}

} // ripple
