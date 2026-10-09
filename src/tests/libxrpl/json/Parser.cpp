#include <xrpl/json/json_parser.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <sstream>
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
 * A visitor that rejects the end of every object and array.
 */
struct RejectContainerEnd
{
    using ReturnType = std::expected<void, std::string>;

    static ReturnType
    onObjectEnd(std::size_t)
    {
        return std::unexpected("rejected object end");
    }

    static ReturnType
    onArrayEnd(std::size_t)
    {
        return std::unexpected("rejected array end");
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

/**
 * Keeps every double exactly, where Trace rounds them for display.
 */
struct Doubles
{
    using ReturnType = std::expected<void, std::string>;

    std::vector<double> values;

    ReturnType
    onDouble(double value)
    {
        values.push_back(value);
        return {};
    }
};

}  // namespace

TEST(JsonParser, reports_events_in_document_order)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON({"b":[1,2.5,true,null],"a":"x"})JSON"}))
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

    ASSERT_TRUE(parser.parse(std::string{R"JSON({"z":1,"m":2,"a":3})JSON"}))
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

    ASSERT_TRUE(parser.parse(std::string{R"JSON({"a":{},"b":[]})JSON"}))
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

    ASSERT_TRUE(parser.parse(std::string{R"JSON([1 /*hi*/ , 2])JSON"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{", "arr[", "int(1)", "cmt(/*hi*/)", "int(2)", "arr]2", "doc}14"}));
}

TEST(JsonParser, no_document_end_when_parsing_fails)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":})JSON"}));

    for (auto const& event : trace.events)
    {
        EXPECT_FALSE(event.starts_with("doc}")) << event;
    }
}

TEST(JsonParser, every_callback_is_optional)
{
    auto silent = Silent{};
    auto parser = json::Parser{silent};

    EXPECT_TRUE(parser.parse(std::string{R"JSON({"a":[1,"b",null,true,2.5]})JSON"}))
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, visitors_are_held_by_reference)
{
    // Whatever a visitor accumulates must be visible through the caller's own
    // object, not a copy owned by the Parser.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON([1])JSON"})) << parser.getFormattedErrorMessages();

    EXPECT_FALSE(trace.events.empty());
    EXPECT_EQ(&parser.visitor<0>(), &trace);
}

TEST(JsonParser, a_rejecting_visitor_fails_the_parse)
{
    auto reject = RejectKey{};
    auto parser = json::Parser{reject};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1})JSON"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("rejected key 'a'"), std::string::npos);
}

TEST(JsonParser, rejection_short_circuits_later_visitors)
{
    // Visitors run in declaration order, and the first failure stops the rest
    // from seeing that event.
    auto reject = RejectKey{};
    auto counter = CountKeys{};
    auto parser = json::Parser{reject, counter};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1})JSON"}));
    EXPECT_EQ(counter.keys, 0u);
}

TEST(JsonParser, earlier_visitors_still_see_the_event)
{
    auto counter = CountKeys{};
    auto reject = RejectKey{};
    auto parser = json::Parser{counter, reject};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1})JSON"}));
    EXPECT_EQ(counter.keys, 1u);
}

TEST(JsonParser, a_rejected_container_end_is_located_at_the_closing_token)
{
    // Empty and non-empty containers must both point at the closing bracket,
    // not the opening one.
    for (auto const& [document, location] : {
             std::pair{R"JSON({   })JSON", "* Line 1, Column 5\n"},
             std::pair{R"JSON({"a":1   })JSON", "* Line 1, Column 10\n"},
             std::pair{R"JSON([   ])JSON", "* Line 1, Column 5\n"},
             std::pair{R"JSON([1   ])JSON", "* Line 1, Column 6\n"},
         })
    {
        auto reject = RejectContainerEnd{};
        auto parser = json::Parser{reject};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_EQ(parser.getFormattedErrorMessages().find(location), 0u)
            << document << ": " << parser.getFormattedErrorMessages();
    }
}

TEST(JsonParser, int_and_uint_split_at_the_signed_boundary)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON([2147483647,2147483648,-2147483648])JSON"}))
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

    EXPECT_FALSE(parser.parse(std::string{R"JSON([4294967296])JSON"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("exceeds the allowable range"), std::string::npos);
}

TEST(JsonParser, rejects_out_of_range_double)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON([1e400])JSON"}));
}

TEST(JsonParser, rejects_a_double_too_small_to_represent)
{
    // Each of these rounds to zero, which would silently lose the value.
    for (auto const* document : {
             R"JSON([1e-400])JSON",
             R"JSON([-1e-400])JSON",
             R"JSON([123e-500])JSON",
             R"JSON([0.001e-400])JSON",
             R"JSON([2e-324])JSON",
         })
    {
        auto doubles = Doubles{};
        auto parser = json::Parser{doubles};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_NE(parser.getFormattedErrorMessages().find("is not a number."), std::string::npos)
            << document << ": " << parser.getFormattedErrorMessages();
        EXPECT_TRUE(doubles.values.empty()) << document;
    }
}

TEST(JsonParser, accepts_zero_and_the_smallest_subnormal)
{
    // A zero significand is zero at any exponent, and the smallest subnormal
    // double is the last value above zero.
    auto doubles = Doubles{};
    auto parser = json::Parser{doubles};

    ASSERT_TRUE(parser.parse(
        std::string{R"JSON([0e-400, 0.000e-500, -0.0, 0.0e400, 4.9406564584124654e-324])JSON"}))
        << parser.getFormattedErrorMessages();

    ASSERT_EQ(doubles.values.size(), 5u);
    EXPECT_EQ(doubles.values[0], 0.0);
    EXPECT_EQ(doubles.values[1], 0.0);
    EXPECT_EQ(doubles.values[2], 0.0);
    EXPECT_TRUE(std::signbit(doubles.values[2]));
    EXPECT_EQ(doubles.values[3], 0.0);
    EXPECT_EQ(doubles.values[4], std::numeric_limits<double>::denorm_min());
}

TEST(JsonParser, decodes_escapes_and_surrogate_pairs)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON(["a\nb\u0041\uD83D\uDE00"])JSON"}))
        << parser.getFormattedErrorMessages();

    ASSERT_EQ(trace.events.size(), 5u);
    EXPECT_EQ(trace.events[2], "str(a\nbA\xF0\x9F\x98\x80)");
}

TEST(JsonParser, rejects_unpaired_trailing_surrogate)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON(["\uDC00"])JSON"}));
}

TEST(JsonParser, rejects_leading_surrogate_followed_by_non_surrogate)
{
    // Without the range check this silently computed the wrong code point.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON(["\uD800\uD800zzzz"])JSON"}));
}

TEST(JsonParser, rejects_trailing_commas)
{
    // An empty key must not make the '}' after a trailing comma look like the
    // end of an empty object.
    for (auto const* document : {
             R"JSON({"":1,})JSON",
             R"JSON({"a":1,})JSON",
             R"JSON({"a":1,"":2,})JSON",
             R"JSON([1,])JSON",
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
    }
}

TEST(JsonParser, accepts_an_empty_key)
{
    for (auto const* document : {
             R"JSON({"":1})JSON",
             R"JSON({"a":1,"":2})JSON",
             R"JSON({"":{}})JSON",
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_TRUE(parser.parse(std::string{document}))
            << document << ": " << parser.getFormattedErrorMessages();
    }
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

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1})JSON"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("document size exceeds"), std::string::npos);
}

TEST(JsonParser, stream_parse_stops_reading_past_document_size_limit)
{
    // The limit spans more than one read chunk, and the stream holds far more
    // than the limit; only one byte past the limit may be consumed.
    auto input = std::istringstream{"[" + std::string(100'000, ' ') + "]"};

    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.documentSizeLimit = 5000;

    EXPECT_FALSE(parser.parse(input));
    EXPECT_NE(parser.getFormattedErrorMessages().find("document size exceeds"), std::string::npos);
    EXPECT_EQ(input.tellg(), std::streampos{5001});
}

TEST(JsonParser, stream_parse_accepts_a_document_at_the_size_limit)
{
    auto input = std::istringstream{R"JSON([1])JSON"};

    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.documentSizeLimit = 3;

    EXPECT_TRUE(parser.parse(input)) << parser.getFormattedErrorMessages();
    EXPECT_TRUE(input.eof());
    EXPECT_FALSE(input.fail());
}

TEST(JsonParser, stream_parse_reads_a_document_spanning_several_chunks)
{
    auto document = std::string{"["};
    for (auto i = 0; i < 10'000; ++i)
    {
        document += "1,";
    }
    document += "1]";
    auto input = std::istringstream{document};

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_TRUE(parser.parse(input)) << parser.getFormattedErrorMessages();
    EXPECT_FALSE(input.fail());
}

TEST(JsonParser, stream_parse_fails_an_empty_stream)
{
    auto input = std::istringstream{};

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(input));
    EXPECT_TRUE(input.fail());
}

TEST(JsonParser, buffer_sequence_split_anywhere_gives_the_same_events)
{
    // The split lands inside every kind of token in turn: keys, strings and
    // their escapes, numbers, literals, comments, a multibyte UTF-8 character,
    // and the two halves of a CRLF line ending.
    auto const document = std::string{
        R"JSON(/*c*/{"key":"a\u00e9\nb","n":-12.5e3,"i":42,"t":true,"z":null,"u":"é€",)JSON"
        "\r\n"
        R"JSON("arr":[1,// line
2]} //tail)JSON"};

    auto whole = Trace{};
    auto wholeParser = json::Parser{whole};
    ASSERT_TRUE(wholeParser.parse(document)) << wholeParser.getFormattedErrorMessages();

    for (auto split = std::size_t{0}; split <= document.size(); ++split)
    {
        auto const buffers = std::vector<boost::asio::const_buffer>{
            boost::asio::buffer(document.data(), split),
            boost::asio::buffer(document.data() + split, document.size() - split),
        };

        auto pieces = Trace{};
        auto parser = json::Parser{pieces};

        ASSERT_TRUE(parser.parse(buffers))
            << "split at " << split << ": " << parser.getFormattedErrorMessages();
        EXPECT_EQ(pieces.events, whole.events) << "split at " << split;
    }
}

TEST(JsonParser, buffer_sequence_reports_error_locations_across_buffers)
{
    // A CRLF split across two buffers is still one line ending.
    auto const buffers = std::vector<boost::asio::const_buffer>{
        boost::asio::buffer(std::string_view{"[1,\r"}),
        boost::asio::buffer(std::string_view{"\n 2,\n"}),
        boost::asio::buffer(std::string_view{"  x]"}),
    };

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(buffers));
    EXPECT_EQ(parser.getFormattedErrorMessages().find("* Line 3, Column 3\n"), 0u)
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, buffer_sequence_ignores_text_after_the_root_in_a_later_buffer)
{
    auto const buffers = std::vector<boost::asio::const_buffer>{
        boost::asio::buffer(std::string_view{"[1] "}),
        boost::asio::buffer(std::string_view{"/*c*/ garbage"}),
        boost::asio::buffer(std::string_view{"more garbage"}),
    };

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(buffers)) << parser.getFormattedErrorMessages();
    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{"doc{", "arr[", "int(1)", "arr]1", "cmt(/*c*/)", "doc}29"}));
}

TEST(JsonParser, buffer_sequence_trailing_comment_in_a_later_buffer_can_be_rejected)
{
    auto const buffers = std::vector<boost::asio::const_buffer>{
        boost::asio::buffer(std::string_view{"[1] "}),
        boost::asio::buffer(std::string_view{"/*c*/"}),
    };

    auto reject = RejectComment{};
    auto parser = json::Parser{reject};

    EXPECT_FALSE(parser.parse(buffers));
    EXPECT_NE(parser.getFormattedErrorMessages().find("rejected comment"), std::string::npos)
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, stream_parse_reports_error_locations_past_the_first_chunk)
{
    // The error sits well beyond the first read chunk, on the third line.
    auto input = std::istringstream{"[1,\n" + std::string(10'000, ' ') + "2,\n  x]"};

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(input));
    EXPECT_EQ(parser.getFormattedErrorMessages().find("* Line 3, Column 3\n"), 0u)
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, stream_parse_reads_to_the_end_after_the_root_value)
{
    // Parsing stops at the end of the root value, but the stream is still read
    // to its end so the document size covers all of it.
    auto input = std::istringstream{"[1] " + std::string(10'000, 'x')};

    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(input)) << parser.getFormattedErrorMessages();
    EXPECT_TRUE(input.eof());
    EXPECT_FALSE(input.fail());
    EXPECT_EQ(trace.events.back(), "doc}10004");
}

TEST(JsonParser, enforces_key_size_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.keySizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"abc":1})JSON"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("key size exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_string_size_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.stringSizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON(["abc"])JSON"}));
    EXPECT_NE(parser.getFormattedErrorMessages().find("string size exceeds"), std::string::npos);
}

TEST(JsonParser, string_size_limit_applies_to_decoded_size)
{
    // Escapes make the raw token longer than the decoded string, which must
    // still be accepted when its decoded size is within the limit.
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.stringSizeLimit = 2;
    parser.keySizeLimit = 2;

    EXPECT_TRUE(parser.parse(std::string{R"JSON({"\n\t":"\u00e9"})JSON"}))
        << parser.getFormattedErrorMessages();
}

TEST(JsonParser, string_size_limit_stops_decoding_early)
{
    // The bad escape is past the limit, so it is never reached.
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.stringSizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON(["abc\q"])JSON"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("string size exceeds"), std::string::npos) << message;
    EXPECT_EQ(message.find("Bad escape sequence"), std::string::npos) << message;
}

TEST(JsonParser, key_size_limit_stops_decoding_early)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.keySizeLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"abc\q":1})JSON"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("key size exceeds"), std::string::npos) << message;
    EXPECT_EQ(message.find("Bad escape sequence"), std::string::npos) << message;
}

TEST(JsonParser, enforces_object_member_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.objectMembersLimit = 1;

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1,"b":2})JSON"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("object member count exceeds"), std::string::npos);
}

TEST(JsonParser, enforces_array_element_limit)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.arrayElementsLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON([1,2,3])JSON"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("array element count exceeds"), std::string::npos);
}

TEST(JsonParser, object_member_limit_rejects_before_the_extra_member_is_parsed)
{
    // The member past the limit is neither decoded nor dispatched, however
    // large its value.
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.objectMembersLimit = 1;

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":1,"b":{"c":[1,2]}})JSON"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("object member count exceeds"), std::string::npos);
    EXPECT_EQ(trace.events, (std::vector<std::string>{"doc{", "obj{", "key(a)", "int(1)"}));
}

TEST(JsonParser, array_element_limit_rejects_before_the_extra_element_is_parsed)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};
    parser.arrayElementsLimit = 2;

    EXPECT_FALSE(parser.parse(std::string{R"JSON([1,2,{"a":[3]}])JSON"}));
    EXPECT_NE(
        parser.getFormattedErrorMessages().find("array element count exceeds"), std::string::npos);
    EXPECT_EQ(trace.events, (std::vector<std::string>{"doc{", "arr[", "int(1)", "int(2)"}));
}

TEST(JsonParser, containers_exactly_at_their_limit_are_accepted)
{
    for (auto const& [document, limit] : {
             std::pair{R"JSON({})JSON", 0u},
             std::pair{R"JSON([])JSON", 0u},
             std::pair{R"JSON({"a":1,"b":2})JSON", 2u},
             std::pair{R"JSON([1,2])JSON", 2u},
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};
        parser.objectMembersLimit = limit;
        parser.arrayElementsLimit = limit;

        EXPECT_TRUE(parser.parse(std::string{document}))
            << document << ": " << parser.getFormattedErrorMessages();
    }
}

TEST(JsonParser, a_zero_limit_rejects_any_member_or_element)
{
    for (auto const* document : {R"JSON({"a":1})JSON", R"JSON([1])JSON"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};
        parser.objectMembersLimit = 0;
        parser.arrayElementsLimit = 0;

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_NE(parser.getFormattedErrorMessages().find("count exceeds"), std::string::npos)
            << document << ": " << parser.getFormattedErrorMessages();
    }
}

TEST(JsonParser, error_messages_carry_a_location)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({
  "a" 1
})JSON"}));

    auto const message = parser.getFormattedErrorMessages();
    EXPECT_NE(message.find("Line 2"), std::string::npos) << message;
}

TEST(JsonParser, no_errors_reported_on_success)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON({"a":1})JSON"}));
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
    ASSERT_TRUE(moved.parse(std::string{R"JSON([1])JSON"})) << moved.getFormattedErrorMessages();

    EXPECT_EQ(&moved.visitor<0>(), &trace);
    EXPECT_FALSE(trace.events.empty());
}

TEST(JsonParser, visitors_rebinds_the_parser_onto_a_new_visitor)
{
    auto first = Trace{};
    auto second = Trace{};
    auto parser = json::Parser{first};

    parser.visitors(second);
    ASSERT_TRUE(parser.parse(std::string{R"JSON([1])JSON"})) << parser.getFormattedErrorMessages();

    EXPECT_EQ(&parser.visitor<0>(), &second);
    EXPECT_FALSE(second.events.empty());
    EXPECT_TRUE(first.events.empty());
}

TEST(JsonParser, a_move_carries_error_locations_across_intact)
{
    // See the equivalent JsonReader test for why the parser is reused.
    auto trace = Trace{};
    auto source = json::Parser{trace};
    ASSERT_FALSE(source.parse(std::string{R"JSON({"a":})JSON"}));
    ASSERT_EQ(source.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << source.getFormattedErrorMessages();

    auto const moved = json::Parser{std::move(source)};

    source = json::Parser{trace};
    ASSERT_TRUE(source.parse(std::string{R"JSON(



[1])JSON"}))
        << source.getFormattedErrorMessages();

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonParser, a_move_assignment_carries_error_locations_across_intact)
{
    // See the equivalent JsonReader test for why the parser is reused.
    auto trace = Trace{};
    auto source = json::Parser{trace};
    ASSERT_FALSE(source.parse(std::string{R"JSON({"a":})JSON"}));

    auto moved = json::Parser{trace};
    ASSERT_TRUE(moved.parse(std::string{R"JSON([1])JSON"})) << moved.getFormattedErrorMessages();
    moved = std::move(source);

    source = json::Parser{trace};
    ASSERT_TRUE(source.parse(std::string{R"JSON(



[1])JSON"}))
        << source.getFormattedErrorMessages();

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonParser, a_move_preserves_error_locations_into_a_caller_owned_buffer)
{
    auto const document = std::string{R"JSON({"a":})JSON"};

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
             R"JSON(/*c*/{"a":1})JSON",   // before the root
             R"JSON({"a":1} /*c*/)JSON",  // trailing
             R"JSON({"a":1} //c)JSON",    // trailing, cpp style
             R"JSON([1 /*c*/, 2])JSON",   // inside an array
             R"JSON({/*c*/"a":1})JSON",   // inside an object
         })
    {
        auto reject = RejectComment{};
        auto parser = json::Parser{reject};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_NE(parser.getFormattedErrorMessages().find("rejected comment"), std::string::npos)
            << document << ": " << parser.getFormattedErrorMessages();
    }
}

TEST(JsonParser, a_malformed_comment_reaches_no_visitor)
{
    // readComment() consumes the candidate before deciding it is not a comment,
    // so onComment must stay behind that check or visitors see the malformed
    // text as a well formed comment event.
    for (auto const* document : {
             R"JSON(/x{})JSON",        // neither * nor /
             R"JSON(/)JSON",           // nothing after the slash
             R"JSON(/* {} )JSON",      // unterminated c style
             R"JSON({} /y)JSON",       // trailing, neither * nor /
             R"JSON([1, /z 2])JSON",   // inside an array
             R"JSON({/w "a":1})JSON",  // inside an object
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        // Whether the document as a whole fails is a separate matter: garbage
        // after a complete value has always been tolerated. What must hold is
        // that no visitor was told this text was a comment.
        parser.parse(std::string{document});

        auto const comments = std::ranges::count_if(
            trace.events, [](std::string const& event) { return event.starts_with("cmt("); });
        EXPECT_EQ(comments, 0) << document << ": got " << comments << " comment event(s)";
    }
}

TEST(JsonParser, a_well_formed_comment_still_reaches_the_visitor)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON(/*c*/{} //trailing)JSON"}))
        << parser.getFormattedErrorMessages();
    EXPECT_EQ(std::ranges::count(trace.events, "cmt(/*c*/)"), 1);
    EXPECT_EQ(std::ranges::count(trace.events, "cmt(//trailing)"), 1);
}

TEST(JsonParser, doc_style_comments_reach_the_visitor_verbatim)
{
    // The visitor receives the comment exactly as written, delimiters
    // included. A line comment ends at '\n' and keeps it, so a CRLF line ending
    // leaves both characters in the comment.
    for (auto const& [document, comment] : {
             std::pair{
                 std::string{R"JSON([1 /// doc
, 2])JSON"},
                 std::string{"/// doc\n"}},
             std::pair{std::string{"[1 // a\r\n, 2]"}, std::string{"// a\r\n"}},
             std::pair{
                 std::string{R"JSON([1 //
, 2])JSON"},
                 std::string{"//\n"}},
             std::pair{std::string{R"JSON([1 /** doc */, 2])JSON"}, std::string{"/** doc */"}},
             std::pair{std::string{R"JSON([1 /**/, 2])JSON"}, std::string{"/**/"}},
             std::pair{std::string{R"JSON([1 /* a ***/, 2])JSON"}, std::string{"/* a ***/"}},
             std::pair{std::string{R"JSON([1 /*/ */, 2])JSON"}, std::string{"/*/ */"}},
             std::pair{
                 std::string{R"JSON([1 // a */
, 2])JSON"},
                 std::string{"// a */\n"}},
             std::pair{std::string{R"JSON([1 /* // a */, 2])JSON"}, std::string{"/* // a */"}},
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        ASSERT_TRUE(parser.parse(document))
            << document << ": " << parser.getFormattedErrorMessages();
        EXPECT_EQ(
            trace.events,
            (std::vector<std::string>{
                "doc{",
                "arr[",
                "int(1)",
                "cmt(" + comment + ")",
                "int(2)",
                "arr]2",
                "doc}" + std::to_string(document.size())}))
            << document;
    }
}

TEST(JsonParser, rejects_a_block_comment_closed_by_an_even_run_of_stars)
{
    // Boost.JSON skips the character after any '*' that is not followed by
    // '/', so in "**/" it skips the second '*' and never sees the '*' that
    // closes the comment. An odd run such as "/* a ***/" closes normally.
    for (auto const* document : {R"JSON([1 /***/, 2])JSON", R"JSON([1 /* a **/, 2])JSON"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;

        auto const comments = std::ranges::count_if(
            trace.events, [](std::string const& event) { return event.starts_with("cmt("); });
        EXPECT_EQ(comments, 0) << document;
    }
}

TEST(JsonParser, block_comments_do_not_nest)
{
    // The first "*/" closes the comment, leaving the second one as stray text.
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON([1 /* a /* b */ c */, 2])JSON"}));
    EXPECT_EQ(std::ranges::count(trace.events, "cmt(/* a /* b */)"), 1);
}

TEST(JsonParser, a_block_comment_needs_its_own_closing_star)
{
    // The '*' that opens the comment cannot also close it.
    for (auto const* document : {R"JSON([1 /*/, 2])JSON", R"JSON([1 /*/)JSON"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;

        auto const comments = std::ranges::count_if(
            trace.events, [](std::string const& event) { return event.starts_with("cmt("); });
        EXPECT_EQ(comments, 0) << document;
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

TEST(JsonParser, ignores_text_after_the_root_value)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON([1,2,3] garbage)JSON"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        trace.events,
        (std::vector<std::string>{
            "doc{", "arr[", "int(1)", "int(2)", "int(3)", "arr]3", "doc}15"}));
}

TEST(JsonParser, rejects_integers_with_leading_zeros)
{
    for (auto const* document : {R"JSON([01])JSON", R"JSON([-01])JSON", R"JSON([007])JSON"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        // Boost reads the leading 0 as a complete number, then rejects the
        // digit after it.
        EXPECT_FALSE(parser.parse(std::string{document})) << document;
    }
}

TEST(JsonParser, rejects_raw_control_characters_in_strings)
{
    for (auto const* document : {"[\"a\tb\"]", "[\"a\nb\"]", "[\"a\x01b\"]"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_EQ(trace.events, (std::vector<std::string>{"doc{", "arr["})) << document;
    }
}

TEST(JsonParser, rejects_invalid_utf8)
{
    // A lone continuation byte, a truncated two byte sequence, and a byte that
    // can never appear in UTF-8.
    for (auto const* document : {"[\"\x80\"]", "[\"\xC3\"]", "[\"\xFF\"]"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse(std::string{document})) << document;
        EXPECT_EQ(trace.events, (std::vector<std::string>{"doc{", "arr["})) << document;
    }
}

TEST(JsonParser, accepts_valid_multibyte_utf8)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON(["é€😀"])JSON"}))
        << parser.getFormattedErrorMessages();

    ASSERT_EQ(trace.events.size(), 5u);
    EXPECT_EQ(trace.events[2], "str(é€😀)");
}

TEST(JsonParser, negative_zero_is_an_integer)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    ASSERT_TRUE(parser.parse(std::string{R"JSON([-0])JSON"})) << parser.getFormattedErrorMessages();

    EXPECT_EQ(trace.events, (std::vector<std::string>{"doc{", "arr[", "int(0)", "arr]1", "doc}4"}));
}

TEST(JsonParser, integer_outside_int_range_is_an_error_quoting_the_token)
{
    // Covers both ends of the 32-bit range and a value too large even for
    // 64 bits, which a parser built on 64-bit integers would turn into a double.
    for (auto const* number :
         {R"JSON(-2147483649)JSON", R"JSON(4294967296)JSON", R"JSON(18446744073709551616)JSON"})
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_FALSE(parser.parse("[" + std::string{number} + "]")) << number;

        auto const message = parser.getFormattedErrorMessages();
        EXPECT_NE(
            message.find("'" + std::string{number} + "' exceeds the allowable range."),
            std::string::npos)
            << message;
    }
}

TEST(JsonParser, doubles_are_correctly_rounded)
{
    // Each literal sits on or next to a rounding boundary, where a fast but
    // inexact conversion picks the wrong neighbour.
    auto doubles = Doubles{};
    auto parser = json::Parser{doubles};

    ASSERT_TRUE(parser.parse(
        std::string{
            R"JSON([0.1, 9007199254740993.0, 2.2250738585072011e-308, 1.7976931348623157e308])JSON"}))
        << parser.getFormattedErrorMessages();

    EXPECT_EQ(
        doubles.values,
        (std::vector<double>{
            0.1, 9007199254740992.0, 2.2250738585072011e-308, 1.7976931348623157e308}));
}

TEST(JsonParser, rejects_a_malformed_number)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON([1-2])JSON"}));
}

TEST(JsonParser, default_depth_limit_is_25)
{
    EXPECT_EQ(json::Parser<Trace>::kDefaultDepthLimit, 25u);

    auto trace = Trace{};
    auto const parser = json::Parser{trace};
    EXPECT_EQ(parser.depthLimit, 25u);
}

TEST(JsonParser, depth_counts_values_not_just_containers)
{
    // The root value is depth 0 and every value inside a container is one
    // deeper, so a scalar in the innermost container counts as a level while
    // an empty innermost container adds none. With the default limit of 25:
    // 25 arrays around a scalar pass and 26 fail, but 26 empty arrays pass and
    // 27 fail.
    auto const nested = [](std::size_t count, std::string_view inner) {
        return std::string(count, '[') + std::string{inner} + std::string(count, ']');
    };

    for (auto const& [document, accepted] : {
             std::pair{nested(25, "1"), true},
             std::pair{nested(26, "1"), false},
             std::pair{nested(26, ""), true},
             std::pair{nested(27, ""), false},
         })
    {
        auto trace = Trace{};
        auto parser = json::Parser{trace};

        EXPECT_EQ(parser.parse(document), accepted)
            << document << ": " << parser.getFormattedErrorMessages();

        if (!accepted)
        {
            EXPECT_NE(
                parser.getFormattedErrorMessages().find("maximum nesting depth exceeded"),
                std::string::npos)
                << document;
        }
    }
}

TEST(JsonParser, formatted_error_has_line_column_and_message)
{
    auto trace = Trace{};
    auto parser = json::Parser{trace};

    EXPECT_FALSE(parser.parse(std::string{R"JSON({"a":})JSON"}));

    // One entry: a location line, then the message indented on the next.
    auto const message = parser.getFormattedErrorMessages();
    EXPECT_TRUE(message.starts_with("* Line 1, Column 6\n  ")) << message;
    EXPECT_TRUE(message.ends_with('\n')) << message;
    EXPECT_EQ(std::ranges::count(message, '\n'), 2) << message;
}

}  // namespace xrpl
