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

#ifndef RIPPLED_RIPPLE_RPC_IMPL_TESTOUTPUT_H
#define RIPPLED_RIPPLE_RPC_IMPL_TESTOUTPUT_H

#include <ripple/rpc/Output.h>
#include <ripple/rpc/impl/JsonWriter.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace RPC {
namespace New {

struct TestOutput : Output
{
    void output (char const* s, size_t length) override
    {
        data.append (s, length);
    }

    std::string data;
};


struct TestOutputSuite : beast::unit_test::suite
{
    TestOutput output_;
    std::unique_ptr <Writer> writer_;

    void setup (std::string const& testName)
    {
        testcase (testName);
        output_.data.clear ();
        writer_ = std::make_unique <Writer> (output_);
    }

    // Test the result and report values.
    void expectResult (std::string const& expected)
    {
        expectResult (output_.data, expected);
    }

    // Test the result and report values.
    void expectResult (std::string const& result, std::string const& expected)
    {
        expect (result == expected,
                "\n" "result:   '" + result + "'" +
                "\n" "expected: '" + expected + "'");
    }
};

} // New
} // RPC
} // ripple

#endif
