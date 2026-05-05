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
    std::string const val = "hello";
    detail::appendJsonValue(dest, val);
    EXPECT_EQ(dest, "\"hello\"");
}

TEST(AppendJsonValue, appends_to_existing)
{
    std::string dest = "prefix:";
    detail::appendJsonValue(dest, 99);
    EXPECT_EQ(dest, "prefix:99");
}

// -- detail::appendEscapedJsonString ------------------------------------------

TEST(AppendEscapedJsonString, plain_string)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, "hello");
    EXPECT_EQ(dest, "\"hello\"");
}

TEST(AppendEscapedJsonString, escapes_double_quote)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, R"(say "hi")");
    EXPECT_EQ(dest, R"("say \"hi\"")");
}

TEST(AppendEscapedJsonString, escapes_backslash)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, R"(a\b)");
    EXPECT_EQ(dest, R"("a\\b")");
}

TEST(AppendEscapedJsonString, escapes_control_characters)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, "line1\nline2\ttab");
    EXPECT_EQ(dest, R"("line1\nline2\ttab")");
}

TEST(AppendEscapedJsonString, escapes_all_named_controls)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, "\b\f\r");
    EXPECT_EQ(dest, R"("\b\f\r")");
}

TEST(AppendEscapedJsonString, escapes_low_control_char_as_unicode)
{
    std::string dest;
    std::string const input(1, '\x01');
    detail::appendEscapedJsonString(dest, input);
    EXPECT_EQ(dest, R"("\u0001")");
}

TEST(AppendEscapedJsonString, empty_string)
{
    std::string dest;
    detail::appendEscapedJsonString(dest, "");
    EXPECT_EQ(dest, R"("")");
}

// -- appendJsonValue with escaping -------------------------------------------

TEST(AppendJsonValue, string_with_quotes_escaped)
{
    std::string dest;
    std::string const val = R"(say "hi")";
    detail::appendJsonValue(dest, val);
    EXPECT_EQ(dest, R"("say \"hi\"")");
}

TEST(AppendJsonValue, string_with_backslash_escaped)
{
    std::string dest;
    std::string const val = R"(path\to\file)";
    detail::appendJsonValue(dest, val);
    EXPECT_EQ(dest, R"("path\\to\\file")");
}

TEST(AppendJsonValue, string_with_newline_escaped)
{
    std::string dest;
    std::string const val = "line1\nline2";
    detail::appendJsonValue(dest, val);
    EXPECT_EQ(dest, R"("line1\nline2")");
}

// -- appendJsonField with key escaping ----------------------------------------

TEST(AppendJsonField, key_with_quotes_escaped)
{
    std::string dest;
    detail::appendJsonField(dest, R"(my"key)", 42);
    EXPECT_EQ(dest, R"("my\"key":42)");
}

TEST(AppendJsonField, key_with_backslash_escaped)
{
    std::string dest;
    detail::appendJsonField(dest, R"(a\b)", true);
    EXPECT_EQ(dest, R"("a\\b":true)");
}

// -- buildJsonPattern --------------------------------------------------------

TEST(BuildJsonPattern, no_params)
{
    auto const pattern = buildJsonPattern("");
    EXPECT_EQ(pattern, "{\"message\": %v }");
}

TEST(BuildJsonPattern, single_string_field)
{
    auto const pattern = buildJsonPattern("", log::param("level", std::string_view("%l")));
    EXPECT_EQ(pattern, "{\"level\":\"%l\", \"message\": %v }");
}

TEST(BuildJsonPattern, multiple_string_fields)
{
    auto const pattern = buildJsonPattern(
        "",
        log::param("level", std::string_view("%l")),
        log::param("channel", std::string_view("%n")));
    EXPECT_EQ(pattern, "{\"level\":\"%l\",\"channel\":\"%n\", \"message\": %v }");
}

TEST(BuildJsonPattern, typed_fields)
{
    auto const pattern = buildJsonPattern("", log::param("enabled", true), log::param("count", 5));
    EXPECT_EQ(pattern, "{\"enabled\":true,\"count\":5, \"message\": %v }");
}

TEST(BuildJsonPattern, many_fields)
{
    auto const pattern = buildJsonPattern(
        "",
        log::param("a", std::string_view("1")),
        log::param("b", std::string_view("2")),
        log::param("c", std::string_view("3")));
    EXPECT_EQ(pattern, "{\"a\":\"1\",\"b\":\"2\",\"c\":\"3\", \"message\": %v }");
}

TEST(BuildJsonPattern, extends_existing_pattern)
{
    auto const base = buildJsonPattern(
        "",
        log::param("level", std::string_view("%l")),
        log::param("channel", std::string_view("%n")));

    auto const extended = buildJsonPattern(base, log::param("source", std::string_view("%s:%#")));
    EXPECT_EQ(
        extended, "{\"level\":\"%l\",\"channel\":\"%n\",\"source\":\"%s:%#\", \"message\": %v }");
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
