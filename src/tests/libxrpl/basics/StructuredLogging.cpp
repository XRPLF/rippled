#include <xrpl/basics/StructuredLogging.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>

#include <string>

using namespace xrpl;

// -- detail::appendJsonValue -------------------------------------------------

TEST(AppendJsonValue, bool_true)
{
    std::string dest;
    detail::appendJsonValue(dest, true);
    EXPECT_EQ(dest, "true");
}

TEST(AppendJsonValue, bool_false)
{
    std::string dest;
    detail::appendJsonValue(dest, false);
    EXPECT_EQ(dest, "false");
}

TEST(AppendJsonValue, integral_positive)
{
    std::string dest;
    detail::appendJsonValue(dest, 42);
    EXPECT_EQ(dest, "42");
}

TEST(AppendJsonValue, integral_negative)
{
    std::string dest;
    detail::appendJsonValue(dest, -7);
    EXPECT_EQ(dest, "-7");
}

TEST(AppendJsonValue, integral_zero)
{
    std::string dest;
    detail::appendJsonValue(dest, 0);
    EXPECT_EQ(dest, "0");
}

TEST(AppendJsonValue, string_quoted)
{
    std::string dest;
    std::string val = "hello";
    detail::appendJsonValue(dest, val);
    EXPECT_EQ(dest, "\"hello\"");
}

TEST(AppendJsonValue, appends_to_existing)
{
    std::string dest = "prefix:";
    detail::appendJsonValue(dest, 99);
    EXPECT_EQ(dest, "prefix:99");
}

// -- JsonLoggingPatternBuilder -----------------------------------------------

TEST(JsonLoggingPatternBuilder, empty_build)
{
    JsonLoggingPatternBuilder builder;
    EXPECT_EQ(builder.build(), "{, \"message\": %v }");
}

TEST(JsonLoggingPatternBuilder, single_string_field)
{
    JsonLoggingPatternBuilder builder;
    builder.add("level", "%l");
    EXPECT_EQ(builder.build(), "{\"level\":\"%l\", \"message\": %v }");
}

TEST(JsonLoggingPatternBuilder, multiple_string_fields)
{
    JsonLoggingPatternBuilder builder;
    builder.add("level", "%l");
    builder.add("channel", "%n");
    EXPECT_EQ(builder.build(), "{\"level\":\"%l\",\"channel\":\"%n\", \"message\": %v }");
}

TEST(JsonLoggingPatternBuilder, typed_fields)
{
    JsonLoggingPatternBuilder builder;
    builder.add("enabled", true);
    builder.add("count", 5);
    EXPECT_EQ(builder.build(), "{\"enabled\":true,\"count\":5, \"message\": %v }");
}

TEST(JsonLoggingPatternBuilder, chaining)
{
    JsonLoggingPatternBuilder builder;
    builder.add("a", "1").add("b", "2").add("c", "3");
    EXPECT_EQ(builder.build(), "{\"a\":\"1\",\"b\":\"2\",\"c\":\"3\", \"message\": %v }");
}

TEST(JsonLoggingPatternBuilder, from_existing_pattern)
{
    JsonLoggingPatternBuilder original;
    original.add("level", "%l").add("channel", "%n");
    auto const pattern = original.build();

    JsonLoggingPatternBuilder extended(pattern);
    extended.add("source", "%s:%#");
    EXPECT_EQ(
        extended.build(),
        "{\"level\":\"%l\",\"channel\":\"%n\",\"source\":\"%s:%#\", \"message\": %v }");
}

// -- log::Parameter / log::param ---------------------------------------------

TEST(LogParameter, string_param)
{
    auto const p = log::param("tx_hash", std::string("ABC123"));
    EXPECT_EQ(p.name(), "tx_hash");
    EXPECT_EQ(p.value(), "ABC123");
}

TEST(LogParameter, int_param)
{
    auto const p = log::param("count", 42);
    EXPECT_EQ(p.name(), "count");
    EXPECT_EQ(p.value(), 42);
}

TEST(LogParameter, bool_param)
{
    auto const p = log::param("active", true);
    EXPECT_EQ(p.name(), "active");
    EXPECT_EQ(p.value(), true);
}

// -- detail::HasToString concept ---------------------------------------------

TEST(HasToString, xrp_amount_satisfies_concept)
{
    static_assert(detail::HasToString<XRPAmount>);
}

TEST(HasToString, number_satisfies_concept)
{
    static_assert(detail::HasToString<Number>);
}

TEST(HasToString, builtin_types_without_adl)
{
    // Built-in types have no associated namespace for ADL, so unless
    // ToString.h is explicitly included they do not satisfy HasToString.
    // They are handled by the fmt::format fallback path instead.
    static_assert(!detail::HasToString<int>);
    static_assert(!detail::HasToString<double>);
}

// -- appendJsonValue with to_string types ------------------------------------

TEST(AppendJsonValue, xrp_amount_quoted)
{
    std::string dest;
    detail::appendJsonValue(dest, XRPAmount{1000});
    EXPECT_EQ(dest, "\"1000\"");
}

TEST(AppendJsonValue, number_quoted)
{
    std::string dest;
    detail::appendJsonValue(dest, Number{25, -3});
    EXPECT_EQ(dest, "\"0.025\"");
}
