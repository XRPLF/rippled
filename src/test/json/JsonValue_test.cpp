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

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

struct JsonValue_test : public beast::unit_test::Suite
{
    void
    testAsBool()
    {
        testcase("asBool");

        using json::Value;

        // Null and empty containers are false.
        BEAST_EXPECT(!Value().asBool());
        BEAST_EXPECT(!Value(json::ValueType::Array).asBool());
        BEAST_EXPECT(!Value(json::ValueType::Object).asBool());

        // Boolean values pass through.
        BEAST_EXPECT(Value(true).asBool());
        BEAST_EXPECT(!Value(false).asBool());

        // Signed integers.
        BEAST_EXPECT(!Value(Value::Int{0}).asBool());
        BEAST_EXPECT(Value(Value::Int{1}).asBool());
        BEAST_EXPECT(Value(Value::Int{-1}).asBool());

        // Unsigned integers. asBool() must read the unsigned union member;
        // in particular values that only exist in the unsigned range (e.g.
        // kMaxUInt, which is negative when reinterpreted as signed) must
        // still be reported as true.
        BEAST_EXPECT(!Value(Value::UInt{0}).asBool());
        BEAST_EXPECT(Value(Value::UInt{1}).asBool());
        BEAST_EXPECT(Value(Value::kMaxUInt).asBool());

        // Reals.
        BEAST_EXPECT(!Value(0.0).asBool());
        BEAST_EXPECT(Value(1.5).asBool());

        // Strings: only the empty string is false.
        BEAST_EXPECT(!Value("").asBool());
        BEAST_EXPECT(Value("false").asBool());
    }

    void
    run() override
    {
        testAsBool();
    }
};

BEAST_DEFINE_TESTSUITE(JsonValue, json, xrpl);

}  // namespace xrpl
