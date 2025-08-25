//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STInteger.h>

namespace ripple {

struct STInteger_test : public beast::unit_test::suite
{
    void
    testUInt8()
    {
        STUInt8 u8(42);
        BEAST_EXPECT(u8.value() == 42);
        BEAST_EXPECT(u8.getText() == "42");
        BEAST_EXPECT(u8.getSType() == STI_UINT8);
        BEAST_EXPECT(u8.getJson(JsonOptions::none) == 42);
    }

    void
    testUInt16()
    {
        STUInt16 u16(65535);
        BEAST_EXPECT(u16.value() == 65535);
        BEAST_EXPECT(u16.getText() == "65535");
        BEAST_EXPECT(u16.getSType() == STI_UINT16);
        BEAST_EXPECT(u16.getJson(JsonOptions::none) == 65535);
    }

    void
    testUInt32()
    {
        STUInt32 u32(1234567890);
        BEAST_EXPECT(u32.value() == 1234567890);
        BEAST_EXPECT(u32.getText() == "1234567890");
        BEAST_EXPECT(u32.getSType() == STI_UINT32);
        BEAST_EXPECT(u32.getJson(JsonOptions::none) == 1234567890);
    }

    void
    testUInt64()
    {
        STUInt64 u64(0x123456789ABCDEF0ull);
        BEAST_EXPECT(u64.value() == 0x123456789ABCDEF0ull);
        BEAST_EXPECT(u64.getText() == "1311768467463790320");
        BEAST_EXPECT(u64.getSType() == STI_UINT64);

        // By default, getJson returns hex string
        auto jsonVal = u64.getJson(JsonOptions::none);
        BEAST_EXPECT(jsonVal.isString());
        BEAST_EXPECT(jsonVal.asString() == "123456789abcdef0");
    }

    void
    testInt32()
    {
        STInt32 i32(-123456789);
        BEAST_EXPECT(i32.value() == -123456789);
        BEAST_EXPECT(i32.getText() == "-123456789");
        BEAST_EXPECT(i32.getSType() == STI_INT32);
        BEAST_EXPECT(i32.getJson(JsonOptions::none) == -123456789);
    }

    void
    testInt64()
    {
        STInt64 i64(-0x123456789ABCDEF0ll);
        BEAST_EXPECT(i64.value() == -0x123456789ABCDEF0ll);
        BEAST_EXPECT(i64.getText() == "-1311768467463790320");
        BEAST_EXPECT(i64.getSType() == STI_INT64);

        // By default, getJson returns hex string
        auto jsonVal = i64.getJson(JsonOptions::none);
        BEAST_EXPECT(jsonVal.isString());
        BEAST_EXPECT(jsonVal.asString() == "-123456789abcdef0");

        // Test STInt64 with positive value, check base 16 output
        {
            STInt64 i64pos(0x7FFFFFFFFFFFFFFFll);  // max int64_t
            BEAST_EXPECT(i64pos.value() == 0x7FFFFFFFFFFFFFFFll);
            BEAST_EXPECT(i64pos.getText() == "9223372036854775807");
            BEAST_EXPECT(i64pos.getSType() == STI_INT64);

            // By default, getJson returns hex string
            auto jsonVal = i64pos.getJson(JsonOptions::none);
            BEAST_EXPECT(jsonVal.isString());
            BEAST_EXPECT(jsonVal.asString() == "7fffffffffffffff");
        }

        // Test STInt64 with base 10 output using sfMaximumAmount
        {
            STInt64 i64ten(sfMaximumAmount, 1234567890123456789ll);
            BEAST_EXPECT(i64ten.value() == 1234567890123456789ll);

            auto jsonVal = i64ten.getJson(JsonOptions::none);
            BEAST_EXPECT(jsonVal.isString());
            BEAST_EXPECT(jsonVal.asString() == "1234567890123456789");
        }
    }

    void
    run() override
    {
        testUInt8();
        testUInt16();
        testUInt32();
        testUInt64();
        testInt32();
        testInt64();
    }
};

BEAST_DEFINE_TESTSUITE(STInteger, protocol, ripple);

}  // namespace ripple
