#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/beast/unit_test/suite.h>

#include <optional>
#include <string>

namespace xrpl {

class ProtocolVersion_test : public beast::unit_test::Suite
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
        {
            testcase("Convert protocol version to string");

            BEAST_EXPECT(to_string(makeProtocol(0, 0)) == "XRPL/0.0");
            BEAST_EXPECT(to_string(makeProtocol(0, 1)) == "XRPL/0.1");
            BEAST_EXPECT(to_string(makeProtocol(1, 3)) == "XRPL/1.3");
            BEAST_EXPECT(to_string(makeProtocol(2, 0)) == "XRPL/2.0");
            BEAST_EXPECT(to_string(makeProtocol(2, 1)) == "XRPL/2.1");
            BEAST_EXPECT(to_string(makeProtocol(10, 10)) == "XRPL/10.10");
            BEAST_EXPECT(to_string(makeProtocol(65535, 65535)) == "XRPL/65535.65535");
        }

        {
            testcase("Convert strings to protocol versions");

            // Invalid versions, either they do not parse as XRPL/N.M or are unsupported.
            check("", "");
            check("RTXP/1.1,RTXP/1.2,RTXP/1.3", "");
            check("XRPL/-2.1,XRPL/0.3,XRPL/2,XRPL/2.01,websocket", "");

            // Mixture of valid, duplicate, and invalid versions.
            check("RTXP/1.3,XRPL/2.1,XRPL/2.0,/XRPL/3.0", "XRPL/2.0,XRPL/2.1");
            check(
                "XRPL/2.0,XRPL/2.0,XRPL/19.4,XRPL/7.89,XRPL/XRPL/3.0,XRPL/2.01,XRPL/-65535.65535",
                "XRPL/2.0,XRPL/7.89,XRPL/19.4");
            check(
                "XRPL/2.0,XRPL/3.0,XRPL/4,XRPL/,XRPL,OPT XRPL/2.2,XRPL/5.67",
                "XRPL/2.0,XRPL/3.0,XRPL/5.67");
        }

        {
            testcase("Protocol version negotiation");

            // Only the highest supported protocol version, if any, is returned.
            BEAST_EXPECT(negotiateProtocolVersion("") == std::nullopt);
            BEAST_EXPECT(negotiateProtocolVersion("XRPL/0.0") == std::nullopt);
            BEAST_EXPECT(negotiateProtocolVersion("RTXP/1.2,XRPL/0.1") == std::nullopt);
            BEAST_EXPECT(
                negotiateProtocolVersion("XRPL/999.999, XRPL/-2.2,WebSocket/1.0") == std::nullopt);
            BEAST_EXPECT(negotiateProtocolVersion("XRPL/2.2") == makeProtocol(2, 2));
            BEAST_EXPECT(
                negotiateProtocolVersion(
                    "RTXP/1.2, XRPL/2.1, XRPL/2.2, XRPL/2.3, XRPL/2.4, XRPL/999.999") ==
                makeProtocol(2, 3));
        }
    }
};

BEAST_DEFINE_TESTSUITE(ProtocolVersion, overlay, xrpl);

}  // namespace xrpl
