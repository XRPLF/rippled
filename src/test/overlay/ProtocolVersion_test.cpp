//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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
#include <ripple/overlay/impl/ProtocolVersion.h>

namespace ripple {

class ProtocolVersion_test : public beast::unit_test::suite
{
private:
    void
    check(std::string const& s, std::string const& answer)
    {
        auto join = [](auto first, auto last) {
            std::string result;
            if (first != last)
            {
                result = to_string(*first++);

                while (first != last)
                    result += "," + to_string(*first++);
            }
            return result;
        };

        auto const result = parseProtocolVersions(s);
        BEAST_EXPECT(join(result.begin(), result.end()) == answer);
    }

public:
    void
    run() override
    {
        testcase("Convert protocol version to string");
        BEAST_EXPECT(to_string(make_protocol(1, 3)) == "XRPL/1.3");
        BEAST_EXPECT(to_string(make_protocol(2, 0)) == "XRPL/2.0");
        BEAST_EXPECT(to_string(make_protocol(2, 1)) == "XRPL/2.1");
        BEAST_EXPECT(to_string(make_protocol(10, 10)) == "XRPL/10.10");

        {
            testcase("Convert strings to protocol versions");

            // Empty string
            check("", "");

            // clang-format off
            check(
                "RTXP/1.1,RTXP/1.2,RTXP/1.3,XRPL/2.1,XRPL/2.0,/XRPL/3.0",
                "XRPL/2.0,XRPL/2.1");
            check(
                "RTXP/0.9,RTXP/1.01,XRPL/0.3,XRPL/2.01,websocket",
                "");
            check(
                "XRPL/2.0,XRPL/2.0,XRPL/19.4,XRPL/7.89,XRPL/XRPL/3.0,XRPL/2.01",
                "XRPL/2.0,XRPL/7.89,XRPL/19.4");
            check(
                "XRPL/2.0,XRPL/3.0,XRPL/4,XRPL/,XRPL,OPT XRPL/2.2,XRPL/5.67",
                "XRPL/2.0,XRPL/3.0,XRPL/5.67");
            // clang-format on
        }

        {
            testcase("Protocol version negotiation");

            BEAST_EXPECT(negotiateProtocolVersion("RTXP/1.2") == std::nullopt);
            BEAST_EXPECT(
                negotiateProtocolVersion("RTXP/1.2, XRPL/2.0, XRPL/2.1") ==
                make_protocol(2, 1));
            BEAST_EXPECT(
                negotiateProtocolVersion("XRPL/2.2") == make_protocol(2, 2));
            BEAST_EXPECT(
                negotiateProtocolVersion(
                    "RTXP/1.2, XRPL/2.2, XRPL/2.3, XRPL/999.999") ==
                make_protocol(2, 2));
            BEAST_EXPECT(
                negotiateProtocolVersion("XRPL/999.999, WebSocket/1.0") ==
                std::nullopt);
            BEAST_EXPECT(negotiateProtocolVersion("") == std::nullopt);
        }
    }
};

BEAST_DEFINE_TESTSUITE(ProtocolVersion, overlay, ripple);

}  // namespace ripple
