//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <ripple/beast/utility/temp_dir.h>
#include <ripple/crypto/csprng.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <streambuf>
#include <test/jtx/Env.h>

namespace ripple {

class CryptoPRNG_test : public beast::unit_test::suite
{
    void
    testGetValues()
    {
        testcase("Get Values");
        try
        {
            auto& engine = crypto_prng();
            auto rand_val = engine();
            BEAST_EXPECT(rand_val >= engine.min());
            BEAST_EXPECT(rand_val <= engine.max());

            uint16_t twoByte{0};
            engine(&twoByte, sizeof(uint16_t));
            pass();
        }
        catch (std::exception&)
        {
            fail();
        }
    }

public:
    void
    run() override
    {
        testGetValues();
    }
};

BEAST_DEFINE_TESTSUITE(CryptoPRNG, core, ripple);

}  // namespace ripple
