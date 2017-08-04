//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

/*
  Note: This file requires clang 5.0 or greater.
        Typical build setup:
        cmake -Dfuzzer_conditions=ON -Dsan='fuzzer;address' -Dtarget=clang.debug <path to proj CMakeLists.txt>
        Typical run:
        ./fulfillment_fuzzer <path to corpus> -max_len=5000 -jobs=4
        An initial fuzz corpus may be generated with the `conditions.py` script.
 */


#include <ripple/conditions/impl/Der.h>
#include <ripple/conditions/Fulfillment.h>

#include <boost/filesystem.hpp>

#include <fstream>

extern "C" int
LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace ripple;
    using namespace ripple::cryptoconditions;
    using namespace ripple::cryptoconditions::der;
    Decoder decoder(Slice{data, size}, TagMode::automatic);

#if FUZZ_TEST_FULFILLMENT
    std::unique_ptr<Fulfillment> f;
    decoder >> f >> eos;
#elif FUZZ_TEST_CONDITION
    Condition c;
    decoder >> c >> eos;
#else
#error("Must define either FUZZ_TEST_CONDITION or FUZZ_TEST_FULFILLMENT")
#endif

    if (decoder.ec() == Error::logicError)
    {
        static boost::filesystem::path model{"logic_error%%%%.dat"};
        auto const fileName = boost::filesystem::unique_path(model);
        std::ofstream ofs;
        ofs.open(fileName.native(), std::ofstream::out | std::ofstream::binary);
        ofs.write((char const*) data, size);
        ofs.close();
    }

    return 0;
}
