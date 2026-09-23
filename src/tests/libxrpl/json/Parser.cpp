#include <xrpl/json/json_parser.h>
#include <xrpl/json/json_value.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl {

namespace {

/**
 * A visitor implementing every documented callback, recording the event
 * stream as a flat list of strings so that a test can assert on both the
 * events and their order.
 */
struct Trace
{
    using ReturnType = std::expected<void, std::string>;

    std::vector<std::string> events;

    ReturnType
    onDocumentBegin()
    {
        events.emplace_back("doc{");
        return {};
    }
    ReturnType
    onDocumentEnd(std::size_t documentSize)
    {
        events.emplace_back("doc}" + std::to_string(documentSize));
        return {};
    }
    ReturnType
    onKey(std::string_view value)
    {
        events.emplace_back("key(" + std::string(value) + ")");
        return {};
    }
    ReturnType
    onString(std::string_view value)
    {
        events.emplace_back("str(" + std::string(value) + ")");
        return {};
    }
    ReturnType
    onComment(std::string_view value)
    {
        events.emplace_back("cmt(" + std::string(value) + ")");
        return {};
    }
    ReturnType
    onInt(json::Value::Int value)
    {
        events.emplace_back("int(" + std::to_string(value) + ")");
        return {};
    }
    ReturnType
    onUInt(json::Value::UInt value)
    {
        events.emplace_back("uint(" + std::to_string(value) + ")");
        return {};
    }
    ReturnType
    onDouble(double value)
    {
        events.emplace_back("dbl(" + std::to_string(value) + ")");
        return {};
    }
    ReturnType
    onBool(bool value)
    {
        events.emplace_back(value ? "true" : "false");
        return {};
    }
    ReturnType
    onNull()
    {
        events.emplace_back("null");
        return {};
    }
    ReturnType
    onObjectBegin()
    {
        events.emplace_back("obj{");
        return {};
    }
    ReturnType
    onObjectEnd(std::size_t memberCount)
    {
        events.emplace_back("obj}" + std::to_string(memberCount));
        return {};
    }
    ReturnType
    onArrayBegin()
    {
        events.emplace_back("arr[");
        return {};
    }
    ReturnType
    onArrayEnd(std::size_t elementCount)
    {
        events.emplace_back("arr]" + std::to_string(elementCount));
        return {};
    }
};

/**
 * A visitor that implements nothing: every callback must be optional.
 */
struct Silent
{
};

/**
 * A visitor that rejects the first key it is offered.
 */
struct RejectKey
{
    using ReturnType = std::expected<void, std::string>;

    static ReturnType
    onKey(std::string_view value)
    {
        return std::unexpected("rejected key '" + std::string(value) + "'");
    }
};

/**
 * Counts how many events it saw, to prove short-circuiting.
 */
struct CountKeys
{
    using ReturnType = std::expected<void, std::string>;

    std::size_t keys{0};

    ReturnType
    onKey(std::string_view)
    {
        ++keys;
        return {};
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Event stream
// ---------------------------------------------------------------------------

TEST(JsonParser, reports_events_in_document_order)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"({"b":[1,2.5,true,null],"a":"x"})"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{",
            "obj{",
            "key(b)",
            "arr[",
            "int(1)",
            "dbl(2.500000)",
            "true",
            "null",
            "arr]4",
            "key(a)",
            "str(x)",
            "obj}2",
            "doc}31"}));
}

TEST(JsonParser, does_not_sort_keys)
{
    // json::Value stores members in a std::map and therefore iterates them
    // sorted. The parser reports them in the order they appear in the text.
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"({"z":1,"m":2,"a":3})"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{",
            "obj{",
            "key(z)",
            "int(1)",
            "key(m)",
            "int(2)",
            "key(a)",
            "int(3)",
            "obj}3",
            "doc}19"}));
}

TEST(JsonParser, empty_containers_are_balanced)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"({"a":{},"b":[]})"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{",
            "obj{",
            "key(a)",
            "obj{",
            "obj}0",
            "key(b)",
            "arr[",
            "arr]0",
            "obj}2",
            "doc}15"}));
}

TEST(JsonParser, reports_comments)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{"[1 /*hi*/ , 2]"})) << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{", "arr[", "int(1)", "cmt(/*hi*/)", "int(2)", "arr]2", "doc}14"}));
}

TEST(JsonParser, no_document_end_when_parsing_fails)
{
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":})"}));

    for (auto const& event : trace.events)
        EXPECT_FALSE(event.starts_with("doc}")) << event;
}

// ---------------------------------------------------------------------------
// Visitors
// ---------------------------------------------------------------------------

TEST(JsonParser, every_callback_is_optional)
{
    Silent silent;
    json::Parser parser{silent};

    EXPECT_TRUE(parser.parse(std::string{R"({"a":[1,"b",null,true,2.5]})"}))
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, visitors_are_held_by_reference)
{
    // Whatever a visitor accumulates must be visible through the caller's own
    // object, not a copy owned by the Parser.
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{"[1]"})) << parser.getFormattedErrorMessages();

    EXPECT_FALSE(trace.events.empty());
    EXPECT_EQ(&parser.visitor<0>(), &trace);
}

TEST(JsonParser, a_rejecting_visitor_fails_the_parse)
{
    RejectKey reject;
    json::Parser parser{reject};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("rejected key 'a'"), std::string::npos);
}

TEST(JsonParser, rejection_short_circuits_later_visitors)
{
    // Visitors run in declaration order, and the first failure stops the rest
    // from seeing that event.
    RejectKey reject;
    CountKeys counter;
    json::Parser parser{reject, counter};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_EQ(counter.keys, 0u);
}

TEST(JsonParser, earlier_visitors_still_see_the_event)
{
    CountKeys counter;
    RejectKey reject;
    json::Parser parser{counter, reject};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_EQ(counter.keys, 1u);
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

TEST(JsonParser, int_and_uint_split_at_the_signed_boundary)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{"[2147483647,2147483648,-2147483648]"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{",
            "arr[",
            "int(2147483647)",
            "uint(2147483648)",
            "int(-2147483648)",
            "arr]3",
            "doc}35"}));
}

TEST(JsonParser, integer_above_uint_range_is_an_error)
{
    // Deliberately an error rather than a promotion to double, matching what
    // json::Value can hold.
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"[4294967296]"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("exceeds the allowable range"), std::string::npos);
}

TEST(JsonParser, rejects_out_of_range_double)
{
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"[1e400]"}));
}

// ---------------------------------------------------------------------------
// Strings and unicode
// ---------------------------------------------------------------------------

TEST(JsonParser, decodes_escapes_and_surrogate_pairs)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"(["a\nb\u0041\uD83D\uDE00"])"}))
        << parser.getFormattedErrorMessages();

    ASSERT_EQ(trace.events.size(), 5u);
    EXPECT_EQ(trace.events[2], "str(a\nbA\xF0\x9F\x98\x80)");
}

TEST(JsonParser, rejects_unpaired_trailing_surrogate)
{
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"(["\uDC00"])"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("unpaired trailing surrogate"), std::string::npos);
}

TEST(JsonParser, rejects_leading_surrogate_followed_by_non_surrogate)
{
    // Without the range check this silently computed the wrong code point.
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"(["\uD800\uD800zzzz"])"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("trailing surrogate to complete"), std::string::npos) << message;
}

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------

TEST(JsonParser, enforces_depth_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.depthLimit = 4;

    EXPECT_TRUE(parser.parse(std::string(4, '[') + std::string(4, ']')))
        << parser.getFormattedErrorMessages();

    Trace deeper;
    json::Parser tooDeep{deeper};
    tooDeep.depthLimit = 4;

    EXPECT_FALSE(tooDeep.parse(std::string(6, '[') + std::string(6, ']')));
    EXPECT_NE(
        tooDeep.getFormattedErrorMessages().find("maximum nesting depth exceeded"),
        std::string::npos);
}

TEST(JsonParser, enforces_document_size_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.documentSizeLimit = 4;

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("document size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_key_size_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.keySizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"({"abc":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("key size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_string_size_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.stringSizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"(["abc"])"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("string size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_object_member_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.objectMembersLimit = 1;

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1,"b":2})"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("object member count exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_array_element_limit)
{
    Trace trace;
    json::Parser parser{trace};
    parser.arrayElementsLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{"[1,2,3]"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("array element count exceeds"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST(JsonParser, error_messages_carry_a_location)
{
    Trace trace;
    json::Parser parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"{\n  \"a\" 1\n}"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("Line 2"), std::string::npos) << message;
    EXPECT_NE(message.find("Missing ':'"), std::string::npos) << message;
}

TEST(JsonParser, no_errors_reported_on_success)
{
    Trace trace;
    json::Parser parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_TRUE(parser.getFormattedErrorMessages().empty());
}

TEST(JsonParser, handles_a_bare_buffer_without_a_terminator)
{
    // parse(begin, end) is public, so the tokenizer may not assume the
    // document is NUL terminated.
    Trace trace;
    json::Parser parser{trace};

    std::vector<char> const buffer{'[', '1', ']'};
    ASSERT_TRUE(parser.parse(buffer.data(), buffer.data() + buffer.size()))
        << parser.getFormattedErrorMessages();

    std::vector<char> const truncated{'['};
    Trace other;
    json::Parser second{other};
    EXPECT_FALSE(second.parse(truncated.data(), truncated.data() + truncated.size()));
}

}  // namespace xrpl
