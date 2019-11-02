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

#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/xor_shift_engine.h>

namespace beast {

class rfc2616_test : public unit_test::suite
{
public:
    void run() override
    {
        testcase("LWS compression & trimming during parsing");

        beast::xor_shift_engine rng(0x243F6A8885A308D3);

        std::vector<std::string> words;

        for (int i = 0; i != 64; ++i)
            words.push_back("X-" + std::to_string(ripple::rand_int(rng, 100, 1000)));

        std::string question;

        for (auto w : words)
        {
            if (!question.empty())
                question += ",";

            question.append(ripple::rand_int(rng, 0, 3), ' ');
            question.append(w);
            question.append(ripple::rand_int(rng, 0, 3), ' ');
        }

        BEAST_EXPECT(beast::rfc2616::split_commas(question) == words);
    }
};

BEAST_DEFINE_TESTSUITE(rfc2616, utility, beast);

}
