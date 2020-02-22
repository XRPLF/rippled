//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions

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

#include <ripple/app/tx/apply.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/Feature.h>
#include <test/jtx/Env.h>

namespace ripple {

class Apply_test : public beast::unit_test::suite
{
public:
    void run() override
    {
        testcase ("Require Fully Canonicial Signature");
        testFullyCanonicalSigs();
    }

    void testFullyCanonicalSigs()
    {
        // Construct a payments w/out a fully-canonical tx
        const std::string non_fully_canonical_tx =
            "12000022000000002400000001201B00497D9C6140000000000F6950684000000"
            "00000000C732103767C7B2C13AD90050A4263745E4BAB2B975417FA22E87780E1"
            "506DDAF21139BE74483046022100E95670988A34C4DB0FA73A8BFD6383872AF43"
            "8C147A62BC8387406298C3EADC1022100A7DC80508ED5A4750705C702A81CBF9D"
            "2C2DC3AFEDBED37BBCCD97BC8C40E08F8114E25A26437D923EEF4D6D815DF9336"
            "8B62E6440848314BB85996936E4F595287774684DC2AC6266024BEF";

        auto ret = strUnHex (non_fully_canonical_tx);
        SerialIter sitTrans (makeSlice(*ret));
        STTx const tx = *std::make_shared<STTx const> (std::ref (sitTrans));

        {
            test::jtx::Env no_fully_canonical (*this,
                test::jtx::supported_amendments() -
                featureRequireFullyCanonicalSig);

            Validity valid = checkValidity(no_fully_canonical.app().getHashRouter(),
                                           tx,
                                           no_fully_canonical.current()->rules(),
                                           no_fully_canonical.app().config()).first;

            if(valid != Validity::Valid)
                fail("Non-Fully canoncial signature was not permitted");
        }

        {
            test::jtx::Env fully_canonical (*this,
                test::jtx::supported_amendments());

            Validity valid = checkValidity(fully_canonical.app().getHashRouter(),
                                           tx,
                                           fully_canonical.current()->rules(),
                                           fully_canonical.app().config()).first;
            if(valid == Validity::Valid)
                fail("Non-Fully canoncial signature was permitted");
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Apply,app,ripple);

} // ripple
