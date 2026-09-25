#include <xrpl/json/json_parser.h>
#include <xrpl/json/json_value.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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
 * A visitor that rejects every comment it is offered.
 */
struct RejectComment
{
    using ReturnType = std::expected<void, std::string>;

    static ReturnType
    onComment(std::string_view value)
    {
        return std::unexpected("rejected comment '" + std::string(value) + "'");
    }
};

/**
 * Counts how many events it saw, to prove short-circuiting.
 */
struct CountKeys
{
    using ReturnType = std::expected<void, std::string>;

    std::size_t keys{};

    ReturnType
    onKey(std::string_view)
    {
        ++keys;
        return {};
    }
};

}  // namespace

TEST(JsonParser, reports_events_in_document_order)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

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
    auto trace = Trace{};
    auto parser = json::Parser{trace};

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
    auto trace = Trace{};
    auto parser = json::Parser{trace};

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
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{"[1 /*hi*/ , 2]"})) << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{", "arr[", "int(1)", "cmt(/*hi*/)", "int(2)", "arr]2", "doc}14"}));
}

TEST(JsonParser, no_document_end_when_parsing_fails)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":})"}));

    for (auto const& event : trace.events)
    {
        EXPECT_FALSE(event.starts_with("doc}")) << event;
    }
}

TEST(JsonParser, every_callback_is_optional)
{
    auto silent = Silent{};
    auto parser = json::Parser{silent};

    EXPECT_TRUE(parser.parse(std::string{R"({"a":[1,"b",null,true,2.5]})"}))
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, visitors_are_held_by_reference)
{
    // Whatever a visitor accumulates must be visible through the caller's own
    // object, not a copy owned by the Parser.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{"[1]"})) << parser.getFormattedErrorMessages();

    EXPECT_FALSE(trace.events.empty());
    EXPECT_EQ(&parser.visitor<0>(), &trace);
}

TEST(JsonParser, a_rejecting_visitor_fails_the_parse)
{
    auto reject = RejectKey{};
    auto parser = json::Parser{reject};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("rejected key 'a'"), std::string::npos);
}

TEST(JsonParser, rejection_short_circuits_later_visitors)
{
    // Visitors run in declaration order, and the first failure stops the rest
    // from seeing that event.
    auto reject = RejectKey{};
    auto counter = CountKeys{};
    auto parser = json::Parser{reject, counter};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_EQ(counter.keys, 0u);
}

TEST(JsonParser, earlier_visitors_still_see_the_event)
{
    auto counter = CountKeys{};
    auto reject = RejectKey{};
    auto parser = json::Parser{counter, reject};

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_EQ(counter.keys, 1u);
}

TEST(JsonParser, int_and_uint_split_at_the_signed_boundary)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

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
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"[4294967296]"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("exceeds the allowable range"), std::string::npos);
}

TEST(JsonParser, rejects_out_of_range_double)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"[1e400]"}));
}

TEST(JsonParser, decodes_escapes_and_surrogate_pairs)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"(["a\nb\u0041\uD83D\uDE00"])"}))
        << parser.getFormattedErrorMessages();

    ASSERT_EQ(trace.events.size(), 5u);
    EXPECT_EQ(trace.events[2], "str(a\nbA\xF0\x9F\x98\x80)");
}

TEST(JsonParser, rejects_unpaired_trailing_surrogate)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"(["\uDC00"])"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("unpaired trailing surrogate"), std::string::npos);
}

TEST(JsonParser, rejects_leading_surrogate_followed_by_non_surrogate)
{
    // Without the range check this silently computed the wrong code point.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"(["\uD800\uD800zzzz"])"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("trailing surrogate to complete"), std::string::npos) << message;
}

TEST(JsonParser, enforces_depth_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.depthLimit = 4;

    EXPECT_TRUE(parser.parse(std::string(4, '[') + std::string(4, ']')))
        << parser.getFormattedErrorMessages();

    auto deeper = Trace{};
    auto tooDeep = json::Parser{deeper};
    tooDeep.depthLimit = 4;

    EXPECT_FALSE(tooDeep.parse(std::string(6, '[') + std::string(6, ']')));
    EXPECT_NE(
        tooDeep.getFormattedErrorMessages().find("maximum nesting depth exceeded"),
        std::string::npos);
}

TEST(JsonParser, enforces_document_size_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.documentSizeLimit = 4;

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("document size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_key_size_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.keySizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"({"abc":1})"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("key size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_string_size_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.stringSizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"(["abc"])"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("string size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_object_member_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.objectMembersLimit = 1;

    EXPECT_FALSE(parser.parse(std::string{R"({"a":1,"b":2})"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("object member count exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_array_element_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.arrayElementsLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{"[1,2,3]"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("array element count exceeds"), std::string::npos);
}

TEST(JsonParser, error_messages_carry_a_location)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{"{\n  \"a\" 1\n}"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("Line 2"), std::string::npos) << message;
    EXPECT_NE(message.find("Missing ':'"), std::string::npos) << message;
}

TEST(JsonParser, no_errors_reported_on_success)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"({"a":1})"}));
    EXPECT_TRUE(parser.getFormattedErrorMessages().empty());
}

TEST(JsonParser, handles_a_bare_buffer_without_a_terminator)
{
    // parse(begin, end) is public, so the tokenizer may not assume the
    // document is NUL terminated.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    std::vector<char> const buffer{'[', '1', ']'};
    ASSERT_TRUE(parser.parse(buffer.data(), buffer.data() + buffer.size()))
        << parser.getFormattedErrorMessages();

    auto const truncated = std::vector<char>{'['};
    auto other = Trace{};
    auto second = json::Parser{other};
    EXPECT_FALSE(second.parse(truncated.data(), truncated.data() + truncated.size()));
}

TEST(JsonParser, is_movable_but_not_copyable)
{
    using P = json::Parser<Trace>;
    static_assert(!std::is_copy_constructible_v<P>);
    static_assert(!std::is_copy_assignable_v<P>);
    static_assert(std::is_nothrow_move_constructible_v<P>);
    static_assert(std::is_nothrow_move_assignable_v<P>);
}

TEST(JsonParser, a_move_keeps_reporting_to_the_same_visitor)
{
    auto trace = Trace{};
    auto source = json::Parser{trace};

    auto moved = json::Parser{std::move(source)};
    ASSERT_TRUE(moved.parse(std::string{"[1]"})) << moved.getFormattedErrorMessages();

    EXPECT_EQ(&moved.visitor<0>(), &trace);
    EXPECT_FALSE(trace.events.empty());
}

TEST(JsonParser, visitors_rebinds_the_parser_onto_a_new_visitor)
{
    auto first = Trace{};
    auto second = Trace{};
    auto parser = json::Parser{first};

    parser.visitors(second);
    ASSERT_TRUE(parser.parse(std::string{"[1]"})) << parser.getFormattedErrorMessages();

    EXPECT_EQ(&parser.visitor<0>(), &second);
    EXPECT_FALSE(second.events.empty());
    EXPECT_TRUE(first.events.empty());
}

TEST(JsonParser, a_move_carries_error_locations_across_intact)
{
    // See the equivalent JsonReader test for why the parser is reused.
    auto trace = Trace{};
    auto source = json::Parser{trace};
    ASSERT_FALSE(source.parse(std::string{R"({"a":})"}));
    ASSERT_EQ(source.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << source.getFormattedErrorMessages();

    auto moved = json::Parser{std::move(source)};

    source = json::Parser{trace};
    ASSERT_TRUE(source.parse(std::string{"\n\n\n\n[1]"})) << source.getFormattedErrorMessages();

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonParser, a_move_preserves_error_locations_into_a_caller_owned_buffer)
{
    auto const document = std::string{R"({"a":})"};

    auto trace = Trace{};
    auto source = json::Parser{trace};
    ASSERT_FALSE(source.parse(document.data(), document.data() + document.size()));

    auto const moved = json::Parser{std::move(source)};

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonParser, a_rejected_comment_fails_the_parse_wherever_it_appears)
{
    // Comments outside any container are skipped by skipCommentTokens, which
    // has to propagate the rejection: a visitor failing onComment leaves the
    // token type as Comment, so the loop cannot use the type to detect it.
    for (auto const* document : {
             "/*c*/{\"a\":1}",   // before the root
             "{\"a\":1} /*c*/",  // trailing
             "{\"a\":1} //c",    // trailing, cpp style
             "[1 /*c*/, 2]",     // inside an array
             "{/*c*/\"a\":1}",   // inside an object
         })
    {
        auto reject = RejectComment{};
        auto parser = json::Parser{reject};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_NE(parser.getFormattedErrorMessages().find("rejected comment"), std::string::npos)
            << document << ": " << parser.getFormattedErrorMessages();
    }
}

TEST(JsonParser, formats_errors_for_an_empty_range)
{
    // A caller can express an empty document either way: an empty
    // std::vector<char> may yield a null data(), leaving begin_, end_ and every
    // recorded location null, or the range may be empty but addressable.
    auto const addressable = std::array<char, 1>{};

    for (auto const* begin : {static_cast<char const*>(nullptr), addressable.data()})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(begin, begin));
        EXPECT_EQ(parser.getFormattedErrorMessages().find("* Line 1, Column 1"), 0u)
            << parser.getFormattedErrorMessages();
    }
}

}  // namespace xrpl
