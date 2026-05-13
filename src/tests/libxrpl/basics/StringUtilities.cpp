#include <xrpl/basics/StringUtilities.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/ToString.h>

#include <gtest/gtest.h>

#include <string>

namespace xrpl {

class StringUtilitiesTest : public ::testing::Test
{
public:
    static void
    testUnHexSuccess(std::string const& strIn, std::string const& strExpected)
    {
        auto rv = strUnHex(strIn);
        EXPECT_TRUE(rv);

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        EXPECT_TRUE(makeSlice(*rv) == makeSlice(strExpected));
    }

    static void
    testUnHexFailure(std::string const& strIn)
    {
        auto rv = strUnHex(strIn);
        EXPECT_TRUE(!rv);
    }

    static void
    testUnHex()
    {
        testUnHexSuccess("526970706c6544", "RippleD");
        testUnHexSuccess("A", "\n");
        testUnHexSuccess("0A", "\n");
        testUnHexSuccess("D0A", "\r\n");
        testUnHexSuccess("0D0A", "\r\n");
        testUnHexSuccess("200D0A", " \r\n");
        testUnHexSuccess("282A2B2C2D2E2F29", "(*+,-./)");

        // Check for things which contain some or only invalid characters
        testUnHexFailure("123X");
        testUnHexFailure("V");
        testUnHexFailure("XRP");
    }

    static void
    testParseUrl()
    {
        // Expected passes.
        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain.empty());
            EXPECT_TRUE(!pUrl.port);
            // RFC 3986:
            // > In general, a URI that uses the generic syntax for authority
            //   with an empty path should be normalized to a path of "/".
            // Do we want to normalize paths?
            EXPECT_TRUE(pUrl.path.empty());
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme:///"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain.empty());
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "lower://domain"));
            EXPECT_TRUE(pUrl.scheme == "lower");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path.empty());
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "UPPER://domain:234/"));
            EXPECT_TRUE(pUrl.scheme == "upper");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(*pUrl.port == 234);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "Mixed://domain/path"));
            EXPECT_TRUE(pUrl.scheme == "mixed");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/path");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://[::1]:123/path"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "::1");
            EXPECT_TRUE(*pUrl.port == 123);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/path");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://user:pass@domain:123/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username == "user");
            EXPECT_TRUE(pUrl.password == "pass");
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(*pUrl.port == 123);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://user@domain:123/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username == "user");
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(*pUrl.port == 123);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://:pass@domain:123/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password == "pass");
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(*pUrl.port == 123);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://domain:123/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(*pUrl.port == 123);  // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://user:pass@domain/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username == "user");
            EXPECT_TRUE(pUrl.password == "pass");
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://user@domain/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username == "user");
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://:pass@domain/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password == "pass");
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://domain/abc:321"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/abc:321");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme:///path/to/file"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain.empty());
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/path/to/file");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://user:pass@domain/path/with/an@sign"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username == "user");
            EXPECT_TRUE(pUrl.password == "pass");
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/path/with/an@sign");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://domain/path/with/an@sign"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "domain");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/path/with/an@sign");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "scheme://:999/"));
            EXPECT_TRUE(pUrl.scheme == "scheme");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == ":999");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/");
        }

        {
            ParsedUrl pUrl;
            EXPECT_TRUE(parseUrl(pUrl, "http://::1:1234/validators"));
            EXPECT_TRUE(pUrl.scheme == "http");
            EXPECT_TRUE(pUrl.username.empty());
            EXPECT_TRUE(pUrl.password.empty());
            EXPECT_TRUE(pUrl.domain == "::0.1.18.52");
            EXPECT_TRUE(!pUrl.port);
            EXPECT_TRUE(pUrl.path == "/validators");
        }

        // Expected fails.
        {
            ParsedUrl pUrl;
            EXPECT_TRUE(!parseUrl(pUrl, ""));
            EXPECT_TRUE(!parseUrl(pUrl, "nonsense"));
            EXPECT_TRUE(!parseUrl(pUrl, "://"));
            EXPECT_TRUE(!parseUrl(pUrl, ":///"));
            EXPECT_TRUE(!parseUrl(pUrl, "scheme://user:pass@domain:65536/abc:321"));
            EXPECT_TRUE(!parseUrl(pUrl, "UPPER://domain:23498765/"));
            EXPECT_TRUE(!parseUrl(pUrl, "UPPER://domain:0/"));
            EXPECT_TRUE(!parseUrl(pUrl, "UPPER://domain:+7/"));
            EXPECT_TRUE(!parseUrl(pUrl, "UPPER://domain:-7234/"));
            EXPECT_TRUE(!parseUrl(pUrl, "UPPER://domain:@#$56!/"));
        }

        {
            std::string const strUrl("s://" + std::string(8192, ':'));
            ParsedUrl pUrl;
            EXPECT_TRUE(!parseUrl(pUrl, strUrl));
        }
    }

    static void
    testToString()
    {
        auto result = to_string("hello");
        EXPECT_TRUE(result == "hello");
    }

    static void
    run()
    {
        testParseUrl();
        testUnHex();
        testToString();
    }
};

TEST_F(StringUtilitiesTest, string_utilities)
{
    run();
}

}  // namespace xrpl
