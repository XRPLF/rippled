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

#include <BeastConfig.h>
#include <ripple/rpc/impl/FieldReader.h>
#include <ripple/rpc/tests/MockContext.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>

namespace ripple {
namespace RPC {

struct FieldReader_test : TestOutputSuite
{
    MockContext mockContext;
    std::unique_ptr<FieldReader> reader;

    void setup (std::string const& testName)
    {
        TestOutputSuite::setup (testName);
        reader = std::make_unique<FieldReader> (mockContext.context());
        params().clear();
    }

    Json::Value& params()
    {
        return mockContext.context().params;
    }

    void run() override
    {
        // TODO:  we can't yet unit test Account reading because we need
        // more of MockContext to be operational...
        {
            setup ("required bool");
            params()[jss::strict] = true;
            bool strict = false;
            expect (readRequired (*reader, strict, jss::strict));
            expect (strict);
            expect (!reader->error);
        }
        {
            setup ("required bool missing");
            bool strict = false;
            expect (!readRequired (*reader, strict, jss::strict));
            expectEquals (reader->error[jss::error], "invalidParams");
            expectEquals (reader->error[jss::error_code], rpcINVALID_PARAMS);
            expectEquals (reader->error[jss::error_message],
                          "Missing field 'strict'.");
        }
        {
            setup ("optional bool");
            params()[jss::strict] = true;
            bool strict = false;
            expect (readOptional (*reader, strict, jss::strict));
            expect (strict);
            expect (!reader->error);
        }
        {
            setup ("optional bool missing");
            bool strict = false;
            expect (readOptional (*reader, strict, jss::strict));
            expect (!strict);
            expect (!reader->error);
        }
        {
            setup ("required string");
            params()[jss::account] = "xyzzy";
            std::string account;
            expect (readRequired (*reader, account, jss::account));
            expectEquals (account, "xyzzy");
        }
        {
            setup ("required vector zero");
            params()[jss::paths] = Json::arrayValue;
            std::vector<std::string> paths;
            expect (readRequired (*reader, paths, jss::paths));
            expectEquals (paths.size(), 0);
        }
        {
            setup ("required vector one");
            params()[jss::paths] = "xyzzy";
            std::vector<std::string> paths;
            expect (readRequired (*reader, paths, jss::paths));
            expectEquals (paths.size(), 1);
            expectEquals (paths[0], "xyzzy");
        }
        {
            setup ("required vector two");
            auto& jvPaths = params()[jss::paths];
            jvPaths.append ("xyzzy");
            jvPaths.append ("wombat");

            std::vector<std::string> paths;
            expect (readRequired (*reader, paths, jss::paths));
            expectEquals (paths.size(), 2);
            expectEquals (paths[0], "xyzzy");
            expectEquals (paths[1], "wombat");
        }
    }
};

BEAST_DEFINE_TESTSUITE(FieldReader, RPC, ripple);

} // RPC
} // ripple
