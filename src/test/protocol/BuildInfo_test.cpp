//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/BuildInfo.h>

namespace ripple {

class BuildInfo_test : public beast::unit_test::suite
{
public:
    void
    testEncodeSoftwareVersion()
    {
        testcase("EncodeSoftwareVersion");

        auto encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.3-b7");

        // the first two bytes identify the particular implementation, 0x183B
        BEAST_EXPECT(
            (encodedVersion & 0xFFFF'0000'0000'0000LLU) ==
            0x183B'0000'0000'0000LLU);

        // the next three bytes: major version, minor version, patch version,
        // 0x010203
        BEAST_EXPECT(
            (encodedVersion & 0x0000'FFFF'FF00'0000LLU) ==
            0x0000'0102'0300'0000LLU);

        // the next two bits:
        {
            // 01 if a beta
            BEAST_EXPECT(
                (encodedVersion & 0x0000'0000'00C0'0000LLU) >> 22 == 0b01);
            // 10 if an RC
            encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.4-rc7");
            BEAST_EXPECT(
                (encodedVersion & 0x0000'0000'00C0'0000LLU) >> 22 == 0b10);
            // 11 if neither an RC nor a beta
            encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.5");
            BEAST_EXPECT(
                (encodedVersion & 0x0000'0000'00C0'0000LLU) >> 22 == 0b11);
        }

        // the next six bits: rc/beta number (1-63)
        encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.6-b63");
        BEAST_EXPECT((encodedVersion & 0x0000'0000'003F'0000LLU) >> 16 == 63);

        // the last two bytes are zeros
        BEAST_EXPECT((encodedVersion & 0x0000'0000'0000'FFFFLLU) == 0);

        // Test some version strings with wrong formats:
        // no rc/beta number
        encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.3-b");
        BEAST_EXPECT((encodedVersion & 0x0000'0000'00FF'0000LLU) == 0);
        // rc/beta number out of range
        encodedVersion = BuildInfo::encodeSoftwareVersion("1.2.3-b64");
        BEAST_EXPECT((encodedVersion & 0x0000'0000'00FF'0000LLU) == 0);
    }

    void
    testIsNewerVersion()
    {
        testcase("IsNewerVersion");
        auto vFF = 0xFFFF'FFFF'FFFF'FFFFLLU;
        BEAST_EXPECT(!BuildInfo::isNewerVersion(vFF));

        auto v159 = BuildInfo::encodeSoftwareVersion("1.5.9");
        BEAST_EXPECT(!BuildInfo::isNewerVersion(v159));

        auto vCurrent = BuildInfo::getEncodedVersion();
        BEAST_EXPECT(!BuildInfo::isNewerVersion(vCurrent));

        auto vMax = BuildInfo::encodeSoftwareVersion("255.255.255");
        BEAST_EXPECT(BuildInfo::isNewerVersion(vMax));
    }

    void
    run() override
    {
        testEncodeSoftwareVersion();
        testIsNewerVersion();
    }
};

BEAST_DEFINE_TESTSUITE(BuildInfo, protocol, ripple);
}  // namespace ripple
