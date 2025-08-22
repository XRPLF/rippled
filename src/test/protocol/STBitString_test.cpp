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
#include <xrpl/protocol/STBitString.h>

namespace ripple {

struct STBitString_test : public beast::unit_test::suite
{
    void
    testConstructionAndBasics()
    {
        testcase("Construction and Basics");

        // Test STBitString<128>
        {
            STBitString<128> bits128;
            BEAST_EXPECT(bits128.isZero());
            BEAST_EXPECT(bits128.size() == 16);

            // Set a bit and check
            auto data = bits128.getBitString();
            data[0] = 0x80;  // set highest bit
            STBitString<128> bits128b(data);
            BEAST_EXPECT(!bits128b.isZero());
            BEAST_EXPECT(bits128b.getBitString()[0] == 0x80);
        }

        // Test STBitString<160>
        {
            STBitString<160> bits160;
            BEAST_EXPECT(bits160.isZero());
            BEAST_EXPECT(bits160.size() == 20);

            auto data = bits160.getBitString();
            data[19] = 0x01;  // set lowest bit
            STBitString<160> bits160b(data);
            BEAST_EXPECT(!bits160b.isZero());
            BEAST_EXPECT(bits160b.getBitString()[19] == 0x01);
        }

        // Test STBitString<192>
        {
            STBitString<192> bits192;
            BEAST_EXPECT(bits192.isZero());
            BEAST_EXPECT(bits192.size() == 24);

            auto data = bits192.getBitString();
            data[12] = 0xAB;
            STBitString<192> bits192b(data);
            BEAST_EXPECT(!bits192b.isZero());
            BEAST_EXPECT(bits192b.getBitString()[12] == 0xAB);
        }

        // Test STBitString<256>
        {
            STBitString<256> bits256;
            BEAST_EXPECT(bits256.isZero());
            BEAST_EXPECT(bits256.size() == 32);

            auto data = bits256.getBitString();
            std::fill(data.begin(), data.end(), 0xFF);
            STBitString<256> bits256b(data);
            BEAST_EXPECT(!bits256b.isZero());
            for (auto b : bits256b.getBitString())
                BEAST_EXPECT(b == 0xFF);
        }
    }

    void
    testEquality()
    {
        testcase("Equality");

        STBitString<128> a;
        STBitString<128> b;
        BEAST_EXPECT(a == b);

        auto data = a.getBitString();
        data[5] = 0xAA;
        STBitString<128> c(data);
        BEAST_EXPECT(!(a == c));
        BEAST_EXPECT(a != c);

        // Test for 192
        STBitString<192> d;
        STBitString<192> e;
        BEAST_EXPECT(d == e);

        auto data192 = d.getBitString();
        data192[7] = 0x11;
        STBitString<192> f(data192);
        BEAST_EXPECT(!(d == f));
        BEAST_EXPECT(d != f);
    }

    void
    testCopyAndAssignment()
    {
        testcase("Copy and Assignment");

        STBitString<256> a;
        auto data = a.getBitString();
        data[10] = 0x55;
        STBitString<256> b(data);

        STBitString<256> c(b);
        BEAST_EXPECT(c == b);

        STBitString<256> d;
        d = b;
        BEAST_EXPECT(d == b);

        // Test for 192
        STBitString<192> e;
        auto data192 = e.getBitString();
        data192[5] = 0x22;
        STBitString<192> f(data192);

        STBitString<192> g(f);
        BEAST_EXPECT(g == f);

        STBitString<192> h;
        h = f;
        BEAST_EXPECT(h == f);
    }

    void
    testToString()
    {
        testcase("ToString");

        STBitString<160> bits;
        auto data = bits.getBitString();
        data[0] = 0x12;
        data[19] = 0x34;
        STBitString<160> bits2(data);

        auto str = bits2.getText();
        BEAST_EXPECT(!str.empty());
        BEAST_EXPECT(
            str.find("1234") != std::string::npos ||
            str.find("34") != std::string::npos);

        // Test for 192
        STBitString<192> bits192;
        auto data192 = bits192.getBitString();
        data192[0] = 0x56;
        data192[23] = 0x78;
        STBitString<192> bits192b(data192);

        auto str192 = bits192b.getText();
        BEAST_EXPECT(!str192.empty());
        BEAST_EXPECT(
            str192.find("5678") != std::string::npos ||
            str192.find("78") != std::string::npos);
    }

    void
    run() override
    {
        testConstructionAndBasics();
        testEquality();
        testCopyAndAssignment();
        testToString();
    }
};

BEAST_DEFINE_TESTSUITE(STBitString, protocol, ripple);

}  // namespace ripple
