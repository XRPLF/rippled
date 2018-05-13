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

#include <test/jtx/Env.h>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/crypto/csprng.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <streambuf>

namespace ripple {

class CryptoPRNG_test : public beast::unit_test::suite
{
    void
    testGetValues()
    {
        testcase ("Get Values");
        try
        {
            auto& engine = crypto_prng();
            auto rand_val = engine();
            BEAST_EXPECT(rand_val >= engine.min());
            BEAST_EXPECT(rand_val <= engine.max());

            uint16_t twoByte {0};
            engine(&twoByte, sizeof(uint16_t));
            pass();
        }
        catch(std::exception&)
        {
            fail();
        }
    }

    void
    testSaveLoad()
    {
        testcase ("Save/Load State");
        try
        {
            // create a temporary path to write crypto state files
            beast::temp_dir td;

            auto stateFile = boost::filesystem::path {td.file("cryptostate")};
            auto& engine = crypto_prng();
            engine.save_state(stateFile.string());

            size_t size_before_load;
            std::string data_before_load, data_after_load;

            {
                boost::system::error_code ec;
                size_before_load = file_size(stateFile, ec);
                if(! BEAST_EXPECTS(!ec, ec.message()))
                    return;
                if(! BEAST_EXPECT(size_before_load > 0))
                    return;

                std::ifstream ifs(
                    stateFile.string(),
                    std::ios::in | std::ios::binary);
                data_before_load =
                    std::string{std::istreambuf_iterator<char>{ifs}, {}};
                BEAST_EXPECT(data_before_load.size() == size_before_load);
            }

            engine.load_state(stateFile.string());

            // load_state actually causes a new state file to be written
            // ...verify it has changed

            {
                boost::system::error_code ec;
                size_t size_after_load = file_size(stateFile, ec);
                if(! BEAST_EXPECTS(!ec, ec.message()))
                    return;
                BEAST_EXPECT(size_after_load == size_before_load);

                std::ifstream ifs(
                    stateFile.string(),
                    std::ios::in | std::ios::binary);
                data_after_load =
                    std::string{std::istreambuf_iterator<char>{ifs}, {}};
                BEAST_EXPECT(data_after_load.size() == size_after_load);
                BEAST_EXPECT(data_after_load != data_before_load);
            }

            // verify the loaded engine works
            engine();
            pass();
        }
        catch(std::exception&)
        {
            fail();
        }
    }

public:
    void run () override
    {
        testGetValues();
        testSaveLoad();
    }
};

BEAST_DEFINE_TESTSUITE (CryptoPRNG, core, ripple);

}  // ripple
