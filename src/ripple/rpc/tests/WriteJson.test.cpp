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

#include <ripple/rpc/impl/WriteJson.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>

namespace ripple {
namespace RPC {

struct WriteJson_test : TestOutputSuite
{
    void runTest (std::string const& name, std::string const& valueDesc)
    {
        setup (name);
        Json::Value value;
        expect (Json::Reader().parse (valueDesc, value));
        auto out = stringOutput (output_);
        writeJson (value, out);

        // Compare with the original version.
        auto expected = Json::FastWriter().write (value);
        expected.resize (expected.size() - 1);
        // For some reason, the FastWriter puts a carriage return on the end of
        // every piece of Json it outputs.

        expectResult (expected);
        expectResult (valueDesc);
        expectResult (jsonAsString (value));
    }

    void runTest (std::string const& name)
    {
        runTest (name, name);
    }

    void run () override
    {
        runTest ("null");
        runTest ("true");
        runTest ("0");
        runTest ("23.5");
        runTest ("string", "\"a string\"");
        runTest ("empty dict", "{}");
        runTest ("empty array", "[]");
        runTest ("array", "[23,4.25,true,null,\"string\"]");
        runTest ("dict", "{\"hello\":\"world\"}");
        runTest ("array dict", "[{}]");
        runTest ("array array", "[[]]");
        runTest ("more complex",
                 "{\"array\":[{\"12\":23},{},null,false,0.5]}");
    }
};

BEAST_DEFINE_TESTSUITE(WriteJson, ripple_basics, ripple);

} // RPC
} // ripple
