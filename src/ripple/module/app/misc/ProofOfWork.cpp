//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

namespace ripple {

SETUP_LOG (ProofOfWork)

// VFALCO TODO Move these to a header
const uint256 ProofOfWork::sMinTarget ("00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");

ProofOfWork::ProofOfWork (const std::string& token,
                          int iterations,
                          uint256 const& challenge,
                          uint256 const& target)
    : mToken (token)
    , mChallenge (challenge)
    , mTarget (target)
    , mIterations (iterations)
{
}

ProofOfWork::ProofOfWork (const std::string& token)
{
    std::vector<std::string> fields;
    boost::split (fields, token, boost::algorithm::is_any_of ("-"));

    if (fields.size () != 5)
        throw std::runtime_error ("invalid token");

    mToken = token;
    mChallenge.SetHex (fields[0]);
    mTarget.SetHex (fields[1]);
    mIterations = beast::lexicalCast <int> (fields[2]);
}

bool ProofOfWork::isValid () const
{
    if ((mIterations <= kMaxIterations) && (mTarget >= sMinTarget))
        return true;

    WriteLog (lsWARNING, ProofOfWork) << "Invalid PoW: " << mIterations << ", " << mTarget;
    return false;
}

std::uint64_t ProofOfWork::getDifficulty (uint256 const& target, int iterations)
{
    // calculate the approximate number of hashes required to solve this proof of work
    if ((iterations > kMaxIterations) || (target < sMinTarget))
    {
        WriteLog (lsINFO, ProofOfWork) << "Iterations:" << iterations;
        WriteLog (lsINFO, ProofOfWork) << "MaxIterat: " << kMaxIterations;
        WriteLog (lsINFO, ProofOfWork) << "Target:    " << target;
        WriteLog (lsINFO, ProofOfWork) << "MinTarget: " << sMinTarget;
        throw std::runtime_error ("invalid proof of work target/iteration");
    }

    // more iterations means more hashes per iteration but also a larger final hash
    std::uint64_t difficulty = iterations + (iterations / 8);

    // Multiply the number of hashes needed by 256 for each leading zero byte in the difficulty
    const unsigned char* ptr = target.begin ();

    while (*ptr == 0)
    {
        difficulty *= 256;
        ++ptr;
    }

    difficulty = (difficulty * 256) / (*ptr + 1);

    return difficulty;
}

static uint256 getSHA512Half (const std::vector<uint256>& vec)
{
    return Serializer::getSHA512Half (vec.front ().begin (), vec.size () * (256 / 8));
}

uint256 ProofOfWork::solve (int maxIterations) const
{
    if (!isValid ())
        throw std::runtime_error ("invalid proof of work target/iteration");

    uint256 nonce;
    RandomNumbers::getInstance ().fill (&nonce);

    std::vector<uint256> buf2;
    buf2.resize (mIterations);

    std::vector<uint256> buf1;
    buf1.resize (3);
    buf1[0] = mChallenge;

    while (maxIterations > 0)
    {
        buf1[1] = nonce;
        buf1[2].zero ();

        for (int i = (mIterations - 1); i >= 0; --i)
        {
            buf1[2] = getSHA512Half (buf1);
            buf2[i] = buf1[2];
        }

        if (getSHA512Half (buf2) <= mTarget)
            return nonce;

        ++nonce;
        --maxIterations;
    }

    return uint256 ();
}

bool ProofOfWork::checkSolution (uint256 const& solution) const
{
    if (mIterations > kMaxIterations)
        return false;

    std::vector<uint256> buf1;
    buf1.push_back (mChallenge);
    buf1.push_back (solution);
    buf1.push_back (uint256 ());

    std::vector<uint256> buf2;
    buf2.resize (mIterations);

    for (int i = (mIterations - 1); i >= 0; --i)
    {
        buf1[2] = getSHA512Half (buf1);
        buf2[i] = buf1[2];
    }

    return getSHA512Half (buf2) <= mTarget;
}

bool ProofOfWork::validateToken (const std::string& strToken)
{
    static boost::regex reToken ("[[:xdigit:]]{64}-[[:xdigit:]]{64}-[[:digit:]]+-[[:digit:]]+-[[:xdigit:]]{64}");
    boost::smatch       smMatch;

    return boost::regex_match (strToken, smMatch, reToken);
}

//------------------------------------------------------------------------------

bool ProofOfWork::calcResultInfo (PowResult powCode, std::string& strToken, std::string& strHuman)
{
    static struct
    {
        PowResult       powCode;
        const char*     cpToken;
        const char*     cpHuman;
    } powResultInfoA[] =
    {
        {   powREUSED,              "powREUSED",                "Proof-of-work has already been used."                  },
        {   powBADNONCE,            "powBADNONCE",              "The solution does not meet the required difficulty."   },
        {   powEXPIRED,             "powEXPIRED",               "Token is expired."                                     },
        {   powCORRUPT,             "powCORRUPT",               "Invalid token."                                        },
        {   powTOOEASY,             "powTOOEASY",               "Difficulty has increased since token was issued."      },

        {   powOK,                  "powOK",                    "Valid proof-of-work."                                  },
    };

    int iIndex  = RIPPLE_ARRAYSIZE (powResultInfoA);

    while (iIndex-- && powResultInfoA[iIndex].powCode != powCode)
        ;

    if (iIndex >= 0)
    {
        strToken    = powResultInfoA[iIndex].cpToken;
        strHuman    = powResultInfoA[iIndex].cpHuman;
    }

    return iIndex >= 0;
}

} // ripple
