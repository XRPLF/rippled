#include <xrpl/beast/core/SemanticVersion.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <locale>
#include <string>
#include <string_view>
#include <vector>

namespace beast {
namespace {

using IdentifierList = SemanticVersion::IdentifierList;

// Version strings are not valid C++ identifiers, so squash their punctuation to
// turn one into a gtest parameter name.
std::string
identifierFor(std::string_view version)
{
    std::string name{version};
    std::ranges::replace_if(
        name, [](char c) { return !std::isalnum(c, std::locale::classic()); }, '_');
    if (!name.empty() && std::isdigit(name.front(), std::locale::classic()))
        name.insert(0, "v_");
    return name;
}

// Pre-release and metadata suffixes, each applied to a "major.minor.patch" base.
// The valid ones leave a well-formed base well-formed; the invalid ones make any
// base malformed.
constexpr auto kValidPreRelease =
    std::to_array<std::string_view>({"", "-1", "-a", "-a1", "-a1.b1", "-ab.cd", "--"});
constexpr auto kInvalidPreRelease =
    std::to_array<std::string_view>({"+", "!", "-", "-!", "-.", "-a.!", "-0.a"});
constexpr auto kValidMetaData = std::to_array<std::string_view>({"", "+a", "+1", "+a.b", "+ab.cd"});
constexpr auto kInvalidMetaData =
    std::to_array<std::string_view>({"!", "+", "++", "+!", "+.", "+a.!"});

// Assembles base + preRelease + metaData and checks whether it parses. A version
// we accept must also round-trip through print().
void
expectParse(
    std::string_view base,
    std::string_view preRelease,
    std::string_view metaData,
    bool shouldPass)
{
    auto const input = std::string{base}.append(preRelease).append(metaData);
    SCOPED_TRACE(::testing::Message() << '"' << input << '"');

    SemanticVersion v;

    if (shouldPass)
    {
        EXPECT_TRUE(v.parse(input));
        EXPECT_EQ(v.print(), input);
    }
    else
    {
        EXPECT_FALSE(v.parse(input));
    }
}

struct ParseCase
{
    std::string_view testName;
    std::string_view base;
    bool shouldPass;
};

std::string
parseCaseName(::testing::TestParamInfo<ParseCase> const& info)
{
    return std::string{info.param.testName};
}

constexpr auto kParseCases = std::to_array<ParseCase>({
    {.testName = "zeroes", .base = "0.0.0", .shouldPass = true},
    {.testName = "simple", .base = "1.2.3", .shouldPass = true},
    {.testName = "max_int", .base = "2147483647.2147483647.2147483647", .shouldPass = true},

    // negative values
    {.testName = "negative_major", .base = "-1.2.3", .shouldPass = false},
    {.testName = "negative_minor", .base = "1.-2.3", .shouldPass = false},
    {.testName = "negative_patch", .base = "1.2.-3", .shouldPass = false},

    // missing parts
    {.testName = "empty", .base = "", .shouldPass = false},
    {.testName = "major_only", .base = "1", .shouldPass = false},
    {.testName = "major_then_dot", .base = "1.", .shouldPass = false},
    {.testName = "major_and_minor", .base = "1.2", .shouldPass = false},
    {.testName = "major_minor_then_dot", .base = "1.2.", .shouldPass = false},
    {.testName = "missing_major", .base = ".2.3", .shouldPass = false},

    // whitespace
    {.testName = "leading_space", .base = " 1.2.3", .shouldPass = false},
    {.testName = "space_after_major", .base = "1 .2.3", .shouldPass = false},
    {.testName = "space_after_minor", .base = "1.2 .3", .shouldPass = false},
    {.testName = "trailing_space", .base = "1.2.3 ", .shouldPass = false},

    // leading zeroes
    {.testName = "leading_zero_in_major", .base = "01.2.3", .shouldPass = false},
    {.testName = "leading_zero_in_minor", .base = "1.02.3", .shouldPass = false},
    {.testName = "leading_zero_in_patch", .base = "1.2.03", .shouldPass = false},
});

struct ValuesCase
{
    std::string_view testName;
    std::string_view input;
    int majorVersion;
    int minorVersion;
    int patchVersion;
    IdentifierList preReleaseIdentifiers{};  // NOLINT(readability-redundant-member-init)
    IdentifierList metaData{};               // NOLINT(readability-redundant-member-init)
};

std::string
valuesCaseName(::testing::TestParamInfo<ValuesCase> const& info)
{
    return std::string{info.param.testName};
}

std::vector<ValuesCase> const kValuesCases{
    {
        .testName = "zero_major",
        .input = "0.1.2",
        .majorVersion = 0,
        .minorVersion = 1,
        .patchVersion = 2,
    },
    {
        .testName = "simple",
        .input = "1.2.3",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
    },
    {
        .testName = "one_pre_release_identifier",
        .input = "1.2.3-rc1",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .preReleaseIdentifiers = {"rc1"},
    },
    {
        .testName = "two_pre_release_identifiers",
        .input = "1.2.3-rc1.debug",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .preReleaseIdentifiers = {"rc1", "debug"},
    },
    {
        .testName = "three_pre_release_identifiers",
        .input = "1.2.3-rc1.debug.asm",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .preReleaseIdentifiers = {"rc1", "debug", "asm"},
    },
    {
        .testName = "one_metadata_identifier",
        .input = "1.2.3+full",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .metaData = {"full"},
    },
    {
        .testName = "two_metadata_identifiers",
        .input = "1.2.3+full.prod",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .metaData = {"full", "prod"},
    },
    {
        .testName = "three_metadata_identifiers",
        .input = "1.2.3+full.prod.x86",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .metaData = {"full", "prod", "x86"},
    },
    {
        .testName = "pre_release_and_metadata",
        .input = "1.2.3-rc1.debug.asm+full.prod.x86",
        .majorVersion = 1,
        .minorVersion = 2,
        .patchVersion = 3,
        .preReleaseIdentifiers = {"rc1", "debug", "asm"},
        .metaData = {"full", "prod", "x86"},
    },
};

struct OrderCase
{
    std::string_view lesser;
    std::string_view greater;
};

std::string
orderCaseName(::testing::TestParamInfo<OrderCase> const& info)
{
    return identifierFor(info.param.lesser) + "_below_" + identifierFor(info.param.greater);
}

constexpr auto kOrderCases = std::to_array<OrderCase>({
    {.lesser = "1.0.0-alpha", .greater = "1.0.0-alpha.1"},
    {.lesser = "1.0.0-alpha.1", .greater = "1.0.0-alpha.beta"},
    {.lesser = "1.0.0-alpha.beta", .greater = "1.0.0-beta"},
    {.lesser = "1.0.0-beta", .greater = "1.0.0-beta.2"},
    {.lesser = "1.0.0-beta.2", .greater = "1.0.0-beta.11"},
    {.lesser = "1.0.0-beta.11", .greater = "1.0.0-rc.1"},
    {.lesser = "1.0.0-rc.1", .greater = "1.0.0"},
    {.lesser = "0.9.9", .greater = "1.0.0"},
});

}  // namespace

class SemanticVersionParse : public ::testing::TestWithParam<ParseCase>
{
};

// Exercises the base string on its own and with every combination of appended
// pre-release identifiers and metadata.
TEST_P(SemanticVersionParse, pre_release_and_metadata_combinations)
{
    auto const& [testName, base, shouldPass] = GetParam();

    for (auto const preRelease : kValidPreRelease)
    {
        for (auto const metaData : kValidMetaData)
            expectParse(base, preRelease, metaData, shouldPass);

        for (auto const metaData : kInvalidMetaData)
            expectParse(base, preRelease, metaData, false);
    }

    // A malformed pre-release section poisons the whole string, whatever
    // metadata follows it.
    for (auto const preRelease : kInvalidPreRelease)
    {
        for (auto const metaData : kValidMetaData)
            expectParse(base, preRelease, metaData, false);

        for (auto const metaData : kInvalidMetaData)
            expectParse(base, preRelease, metaData, false);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Inputs,
    SemanticVersionParse,
    ::testing::ValuesIn(kParseCases),
    parseCaseName);

class SemanticVersionValues : public ::testing::TestWithParam<ValuesCase>
{
};

TEST_P(SemanticVersionValues, decomposes_into_components)
{
    auto const& expected = GetParam();

    SemanticVersion v;
    EXPECT_TRUE(v.parse(expected.input));

    EXPECT_EQ(v.majorVersion, expected.majorVersion);
    EXPECT_EQ(v.minorVersion, expected.minorVersion);
    EXPECT_EQ(v.patchVersion, expected.patchVersion);

    EXPECT_EQ(v.preReleaseIdentifiers, expected.preReleaseIdentifiers);
    EXPECT_EQ(v.metaData, expected.metaData);
}

INSTANTIATE_TEST_SUITE_P(
    Inputs,
    SemanticVersionValues,
    ::testing::ValuesIn(kValuesCases),
    valuesCaseName);

class SemanticVersionOrder : public ::testing::TestWithParam<OrderCase>
{
};

TEST_P(SemanticVersionOrder, lesser_precedes_greater)
{
    auto const& [lesser, greater] = GetParam();

    // Metadata takes no part in precedence, so attaching it to either side must
    // leave the ordering untouched.
    static constexpr auto kMetaData = std::to_array<std::string_view>({"", "+meta"});

    for (auto const lesserMetaData : kMetaData)
    {
        for (auto const greaterMetaData : kMetaData)
        {
            auto const lesserInput = std::string{lesser}.append(lesserMetaData);
            auto const greaterInput = std::string{greater}.append(greaterMetaData);
            SCOPED_TRACE(
                ::testing::Message() << '"' << lesserInput << "\" < \"" << greaterInput << '"');

            SemanticVersion lesserVersion;
            SemanticVersion greaterVersion;
            EXPECT_TRUE(lesserVersion.parse(lesserInput));
            EXPECT_TRUE(greaterVersion.parse(greaterInput));

            EXPECT_EQ(compare(lesserVersion, lesserVersion), 0);
            EXPECT_EQ(compare(greaterVersion, greaterVersion), 0);
            EXPECT_LT(compare(lesserVersion, greaterVersion), 0);
            EXPECT_GT(compare(greaterVersion, lesserVersion), 0);

            EXPECT_LT(lesserVersion, greaterVersion);
            EXPECT_GT(greaterVersion, lesserVersion);
            EXPECT_EQ(lesserVersion, lesserVersion);
            EXPECT_EQ(greaterVersion, greaterVersion);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Pairs,
    SemanticVersionOrder,
    ::testing::ValuesIn(kOrderCases),
    orderCaseName);

}  // namespace beast
