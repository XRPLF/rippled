#include <xrpl/basics/StringUtilities.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/ToString.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
        EXPECT_EQ(makeSlice(*rv), makeSlice(strExpected));
    }

    static void
    testUnHexFailure(std::string const& strIn)
    {
        auto rv = strUnHex(strIn);
        EXPECT_FALSE(rv);
    }
};

TEST_F(StringUtilitiesTest, un_hex)
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

// Everything a URL is expected to parse into. The components a case does not
// mention default to "absent", so each row lists only what it is about.
struct ParseUrlCase
{
    std::string_view name;
    std::string_view url;
    std::string_view scheme;
    std::string_view username = {};          // NOLINT(readability-redundant-member-init)
    std::string_view password = {};          // NOLINT(readability-redundant-member-init)
    std::string_view domain = {};            // NOLINT(readability-redundant-member-init)
    std::optional<std::uint16_t> port = {};  // NOLINT(readability-redundant-member-init)
    std::string_view path = {};              // NOLINT(readability-redundant-member-init)
};

constexpr auto kParseUrlCases = std::to_array<ParseUrlCase>({
    // RFC 3986:
    // > In general, a URI that uses the generic syntax for authority
    //   with an empty path should be normalized to a path of "/".
    // Do we want to normalize paths? Today an absent path stays absent.
    {
        .name = "scheme_only",
        .url = "scheme://",
        .scheme = "scheme",
    },
    {
        .name = "empty_authority_with_root_path",
        .url = "scheme:///",
        .scheme = "scheme",
        .path = "/",
    },
    {
        .name = "lowercase_scheme",
        .url = "lower://domain",
        .scheme = "lower",
        .domain = "domain",
    },
    {
        .name = "uppercase_scheme_is_lowercased",
        .url = "UPPER://domain:234/",
        .scheme = "upper",
        .domain = "domain",
        .port = 234,
        .path = "/",
    },
    {
        .name = "mixed_case_scheme_is_lowercased",
        .url = "Mixed://domain/path",
        .scheme = "mixed",
        .domain = "domain",
        .path = "/path",
    },
    {
        .name = "bracketed_ipv6_keeps_its_port",
        .url = "scheme://[::1]:123/path",
        .scheme = "scheme",
        .domain = "::1",
        .port = 123,
        .path = "/path",
    },
    {
        .name = "username_and_password_with_port",
        .url = "scheme://user:pass@domain:123/abc:321",
        .scheme = "scheme",
        .username = "user",
        .password = "pass",
        .domain = "domain",
        .port = 123,
        .path = "/abc:321",
    },
    {
        .name = "username_only_with_port",
        .url = "scheme://user@domain:123/abc:321",
        .scheme = "scheme",
        .username = "user",
        .domain = "domain",
        .port = 123,
        .path = "/abc:321",
    },
    {
        .name = "password_only_with_port",
        .url = "scheme://:pass@domain:123/abc:321",
        .scheme = "scheme",
        .password = "pass",
        .domain = "domain",
        .port = 123,
        .path = "/abc:321",
    },
    {
        .name = "no_credentials_with_port",
        .url = "scheme://domain:123/abc:321",
        .scheme = "scheme",
        .domain = "domain",
        .port = 123,
        .path = "/abc:321",
    },
    {
        .name = "username_and_password_without_port",
        .url = "scheme://user:pass@domain/abc:321",
        .scheme = "scheme",
        .username = "user",
        .password = "pass",
        .domain = "domain",
        .path = "/abc:321",
    },
    {
        .name = "username_only_without_port",
        .url = "scheme://user@domain/abc:321",
        .scheme = "scheme",
        .username = "user",
        .domain = "domain",
        .path = "/abc:321",
    },
    {
        .name = "password_only_without_port",
        .url = "scheme://:pass@domain/abc:321",
        .scheme = "scheme",
        .password = "pass",
        .domain = "domain",
        .path = "/abc:321",
    },
    {
        .name = "no_credentials_without_port",
        .url = "scheme://domain/abc:321",
        .scheme = "scheme",
        .domain = "domain",
        .path = "/abc:321",
    },
    {
        .name = "empty_authority_with_file_path",
        .url = "scheme:///path/to/file",
        .scheme = "scheme",
        .path = "/path/to/file",
    },
    // The '@' separating credentials from the domain is the first one, so a
    // later '@' belongs to the path.
    {
        .name = "at_sign_in_path_with_credentials",
        .url = "scheme://user:pass@domain/path/with/an@sign",
        .scheme = "scheme",
        .username = "user",
        .password = "pass",
        .domain = "domain",
        .path = "/path/with/an@sign",
    },
    {
        .name = "at_sign_in_path_without_credentials",
        .url = "scheme://domain/path/with/an@sign",
        .scheme = "scheme",
        .domain = "domain",
        .path = "/path/with/an@sign",
    },
    // A port with no domain in front of it is not recognised as a port at all;
    // the whole ":999" becomes the domain.
    {
        .name = "port_without_domain_is_taken_as_domain",
        .url = "scheme://:999/",
        .scheme = "scheme",
        .domain = ":999",
        .path = "/",
    },
    // An unbracketed IPv6 address is parsed as an address, not as host:port, so
    // the trailing ":1234" is folded into the address itself.
    {
        .name = "unbracketed_ipv6_absorbs_the_trailing_port",
        .url = "http://::1:1234/validators",
        .scheme = "http",
        .domain = "::0.1.18.52",
        .path = "/validators",
    },
});

class ParseUrlTest : public ::testing::TestWithParam<ParseUrlCase>
{
};

TEST_P(ParseUrlTest, splits_url_into_components)
{
    auto const& testCase = GetParam();

    ParsedUrl parsed;
    ASSERT_TRUE(parseUrl(parsed, std::string{testCase.url}));

    EXPECT_EQ(parsed.scheme, testCase.scheme);
    EXPECT_EQ(parsed.username, testCase.username);
    EXPECT_EQ(parsed.password, testCase.password);
    EXPECT_EQ(parsed.domain, testCase.domain);
    EXPECT_EQ(parsed.port, testCase.port);
    EXPECT_EQ(parsed.path, testCase.path);
}

INSTANTIATE_TEST_SUITE_P(
    StringUtilities,
    ParseUrlTest,
    ::testing::ValuesIn(kParseUrlCases),
    [](::testing::TestParamInfo<ParseUrlCase> const& info) {
        return std::string{info.param.name};
    });

struct ParseUrlFailureCase
{
    std::string_view name;
    std::string_view url;
};

constexpr auto kParseUrlFailureCases = std::to_array<ParseUrlFailureCase>({
    {.name = "empty", .url = ""},
    {.name = "no_scheme_separator", .url = "nonsense"},
    {.name = "empty_scheme", .url = "://"},
    {.name = "empty_scheme_with_path", .url = ":///"},
    {.name = "port_one_above_the_16_bit_range", .url = "scheme://user:pass@domain:65536/abc:321"},
    {.name = "port_far_above_the_16_bit_range", .url = "UPPER://domain:23498765/"},
    {.name = "port_zero", .url = "UPPER://domain:0/"},
    {.name = "port_with_a_plus_sign", .url = "UPPER://domain:+7/"},
    {.name = "negative_port", .url = "UPPER://domain:-7234/"},
    {.name = "port_of_punctuation", .url = "UPPER://domain:@#$56!/"},
});

class ParseUrlFailureTest : public ::testing::TestWithParam<ParseUrlFailureCase>
{
};

TEST_P(ParseUrlFailureTest, rejects_url)
{
    ParsedUrl parsed;
    EXPECT_FALSE(parseUrl(parsed, std::string{GetParam().url}));
}

INSTANTIATE_TEST_SUITE_P(
    StringUtilities,
    ParseUrlFailureTest,
    ::testing::ValuesIn(kParseUrlFailureCases),
    [](::testing::TestParamInfo<ParseUrlFailureCase> const& info) {
        return std::string{info.param.name};
    });

TEST_F(StringUtilitiesTest, parse_url_rejects_an_overlong_authority)
{
    ParsedUrl parsed;
    EXPECT_FALSE(parseUrl(parsed, "s://" + std::string(8192, ':')));
}

TEST_F(StringUtilitiesTest, to_string)
{
    auto result = to_string("hello");
    EXPECT_EQ(result, "hello");
}

TEST_F(StringUtilitiesTest, trim_whitespace)
{
    EXPECT_EQ(trimWhitespace(""), "");
    EXPECT_EQ(trimWhitespace("   "), "");
    EXPECT_EQ(trimWhitespace("abc"), "abc");
    EXPECT_EQ(trimWhitespace("  abc"), "abc");
    EXPECT_EQ(trimWhitespace("abc  "), "abc");
    EXPECT_EQ(trimWhitespace(" \t\n\v\f\r abc \t\n\v\f\r "), "abc");

    // Interior whitespace is preserved.
    EXPECT_EQ(trimWhitespace("  a b\tc  "), "a b\tc");
}

TEST_F(StringUtilitiesTest, to_lower)
{
    EXPECT_EQ(toLower(""), "");
    EXPECT_EQ(toLower("ABC"), "abc");
    EXPECT_EQ(toLower("AbC123"), "abc123");
    EXPECT_EQ(toLower("already lower"), "already lower");

    // Only 'A'-'Z' are remapped. Neighbouring punctuation and digits, which a
    // buggy range check could catch, must survive untouched.
    EXPECT_EQ(toLower("@[`{_^"), "@[`{_^");
}

// Both helpers are documented as depending only on their input. Guard that by
// checking the bytes just outside ASCII, which a locale-aware isspace/tolower
// could classify differently.
TEST_F(StringUtilitiesTest, trim_and_lower_ignore_locale)
{
    // 0xA0 is NO-BREAK SPACE in Latin-1 and is whitespace to some locales.
    std::string const nbsp("\xA0", 1);
    EXPECT_EQ(trimWhitespace(nbsp), nbsp);
    EXPECT_EQ(trimWhitespace(" " + nbsp + " "), nbsp);

    // 0xC0 is LATIN CAPITAL LETTER A WITH GRAVE in Latin-1.
    std::string const agrave("\xC0", 1);
    EXPECT_EQ(toLower(agrave), agrave);
}

}  // namespace xrpl
