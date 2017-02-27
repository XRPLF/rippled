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
#include <ripple/beast/unit_test.h>
#include <ripple/crypto/csprng.h>
#include <boost/scope_exit.hpp>
#include <boost/filesystem.hpp>

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
            using namespace boost::filesystem;
            boost::system::error_code ec;
            // create a temporary path to write ledger files in
            auto path  = temp_directory_path(ec);
            if(! BEAST_EXPECTS(!ec, ec.message()))
                return;
            path /= unique_path("%%%%-%%%%-%%%%-%%%%", ec);
            if(! BEAST_EXPECTS(!ec, ec.message()))
                return;
            create_directories(path, ec);
            if(! BEAST_EXPECTS(!ec, ec.message()))
                return;

            BOOST_SCOPE_EXIT(ec, path, this_ ) {
                remove_all(path, ec);
                this_->expect(!ec, ec.message(), __FILE__, __LINE__);
            } BOOST_SCOPE_EXIT_END

            auto stateFile = path / "cryptostate";
            auto& engine = crypto_prng();
            engine.mix_entropy();
            engine.save_state(stateFile.string());

            auto size = file_size(stateFile, ec);
            if(! BEAST_EXPECTS(!ec, ec.message()))
                return;
            if(! BEAST_EXPECT(size > 0))
                return;

            engine.load_state(stateFile.string());
            auto rand_val = engine();
            pass();
        }
        catch(std::exception&)
        {
            fail();
        }
    }

public:
    void run ()
    {
        testGetValues();
        testSaveLoad();
    }
};

BEAST_DEFINE_TESTSUITE (CryptoPRNG, core, ripple);

}  // ripple
