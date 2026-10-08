#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

// Reader delegates tokenizing to json::Parser and keeps only the policy that
// belongs to the json::Value representation. These tests cover that policy,
// plus the tree it builds.

TEST(JsonReader, rejects_duplicate_keys)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    EXPECT_FALSE(reader.parse(std::string{R"JSON({"a":1,"a":2})JSON"}, root));
    EXPECT_NE(reader.getFormattedErrorMessages().find("appears twice"), std::string::npos)
        << reader.getFormattedErrorMessages();
}

TEST(JsonReader, rejects_duplicate_keys_in_a_nested_object)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    EXPECT_FALSE(reader.parse(std::string{R"JSON({"outer":{"a":1,"a":2}})JSON"}, root));
}

TEST(JsonReader, allows_the_same_key_in_sibling_objects)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"JSON({"x":{"a":1},"y":{"a":2}})JSON"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root["x"]["a"].asInt(), 1);
    EXPECT_EQ(root["y"]["a"].asInt(), 2);
}

TEST(JsonReader, requires_an_object_array_or_null_document)
{
    for (auto const* scalar : {
             R"JSON("a string")JSON",
             R"JSON(42)JSON",
             R"JSON(2.5)JSON",
             R"JSON(true)JSON",
             R"JSON(false)JSON",
         })
    {
        auto root = json::Value{};
        auto reader = json::Reader{};

        EXPECT_FALSE(reader.parse(std::string{scalar}, root)) << scalar;
        EXPECT_NE(
            reader.getFormattedErrorMessages().find("must be either an array or an object"),
            std::string::npos)
            << scalar;
    }
}

TEST(JsonReader, reports_a_rejected_bare_scalar_at_the_start_of_the_document)
{
    // The rejection comes from onDocumentEnd, by which point the parser has
    // consumed the value, so it must not be pinned to the end-of-input token.
    for (auto const* scalar : {
             R"JSON(42)JSON",
             R"JSON("a string")JSON",
             R"JSON(true)JSON",
             R"JSON(  42)JSON",
             "\n\n  42",
         })
    {
        auto root = json::Value{};
        auto reader = json::Reader{};

        ASSERT_FALSE(reader.parse(std::string{scalar}, root)) << scalar;
        EXPECT_EQ(reader.getFormattedErrorMessages().find("* Line 1, Column 1"), 0u)
            << scalar << ": " << reader.getFormattedErrorMessages();
    }

    for (auto const* document : {
             R"JSON({})JSON",
             R"JSON([])JSON",
             R"JSON(null)JSON",
             R"JSON({"a":1})JSON",
             R"JSON([1,2])JSON",
         })
    {
        auto root = json::Value{};
        auto reader = json::Reader{};

        EXPECT_TRUE(reader.parse(std::string{document}, root))
            << document << ": " << reader.getFormattedErrorMessages();
    }
}

TEST(JsonReader, builds_the_expected_tree)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"JSON({"b":[1,2.5,true,null],"a":"x"})JSON"}, root))
        << reader.getFormattedErrorMessages();

    ASSERT_TRUE(root.isObject());
    EXPECT_EQ(root["a"].asString(), "x");

    ASSERT_TRUE(root["b"].isArray());
    ASSERT_EQ(root["b"].size(), 4u);
    EXPECT_EQ(root["b"][0u].asInt(), 1);
    EXPECT_EQ(root["b"][1u].asDouble(), 2.5);
    EXPECT_TRUE(root["b"][2u].asBool());
    EXPECT_TRUE(root["b"][3u].isNull());
}

TEST(JsonReader, members_come_back_sorted_regardless_of_document_order)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"JSON({"z":1,"m":2,"a":3})JSON"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root.getMemberNames(), (std::vector<std::string>{"a", "m", "z"}));
}

TEST(JsonReader, is_reusable_across_parses)
{
    auto reader = json::Reader{};

    {
        auto root = json::Value{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"a":1})JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["a"].asInt(), 1);
    }

    {
        // A failure must not leak errors into the next parse.
        auto root = json::Value{};
        EXPECT_FALSE(reader.parse(std::string{R"JSON({"a":})JSON"}, root));
        EXPECT_FALSE(reader.getFormattedErrorMessages().empty());
    }

    {
        auto root = json::Value{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"b":[2]})JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["b"][0u].asInt(), 2);
        EXPECT_FALSE(root.isMember("a"));
        EXPECT_TRUE(reader.getFormattedErrorMessages().empty());
    }
}

TEST(JsonReader, parses_from_a_character_range)
{
    auto const document = std::string{R"JSON({"a":1})JSON"};

    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(document.data(), document.data() + document.size(), root))
        << reader.getFormattedErrorMessages();
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, parses_from_a_stream)
{
    auto input = std::istringstream{R"JSON({"a":1})JSON"};

    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(input, root)) << reader.getFormattedErrorMessages();
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, parses_a_buffer_sequence_from_a_temporary_reader)
{
    // ServerHandler parses request bodies this way: a temporary Reader over a
    // multi-fragment buffer sequence.
    auto const head = std::string{R"JSON({"a":)JSON"};
    auto const tail = std::string{R"JSON(1})JSON"};

    auto const buffers = std::vector<boost::asio::const_buffer>{
        boost::asio::const_buffer{head.data(), head.size()},
        boost::asio::const_buffer{tail.data(), tail.size()},
    };

    auto root = json::Value{};
    ASSERT_TRUE(json::Reader{}.parse(root, buffers));
    EXPECT_TRUE(root.isObject());
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, stream_extraction_throws_on_bad_input)
{
    auto root = json::Value{};
    auto good = std::istringstream{R"JSON({"a":1})JSON"};
    EXPECT_NO_THROW(good >> root);
    EXPECT_EQ(root["a"].asInt(), 1);

    auto bad = std::istringstream{R"JSON({"a":)JSON"};
    auto other = json::Value{};
    EXPECT_ANY_THROW(bad >> other);
}

// ---------------------------------------------------------------------------
// Invariants inherited from the pre-Parser reader
// ---------------------------------------------------------------------------

namespace {

std::string
nestedObject(unsigned depth)
{
    auto s = std::string{"{"};
    for (unsigned i = 0; i < depth; ++i)
    {
        s += R"JSON("o":{)JSON";
    }
    for (unsigned i = 0; i < depth; ++i)
    {
        s += '}';
    }
    return s + "}";
}

}  // namespace

TEST(JsonReader, enforces_the_nesting_limit_at_the_boundary)
{
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        EXPECT_TRUE(reader.parse(nestedObject(json::Reader::kNestLimit), root))
            << reader.getFormattedErrorMessages();
    }

    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        EXPECT_FALSE(reader.parse(nestedObject(json::Reader::kNestLimit + 1), root));
        EXPECT_NE(
            reader.getFormattedErrorMessages().find("maximum nesting depth"), std::string::npos)
            << reader.getFormattedErrorMessages();
    }
}

TEST(JsonReader, accepts_comments)
{
    // The reader deliberately accepts a superset of strict JSON.
    for (auto const* document : {
             R"JSON({/*c*/"a":1})JSON",
             R"JSON({"a":/*c*/1})JSON",
             R"JSON({"a":1/*c*/})JSON",
             R"JSON({"a":1} /* trailing */)JSON",
             R"JSON({"a":1} // trailing)JSON",
         })
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{document}, root))
            << document << ": " << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["a"].asInt(), 1) << document;
    }

    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"a":1,/*c*/"b":2})JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["b"].asInt(), 2);
    }

    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{"[1,//x\n2]"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root.size(), 2u);
    }
}

TEST(JsonReader, rejects_comments_in_the_two_places_it_never_allowed_them)
{
    // Comments are skipped where a value is expected, but the member separator
    // and the first element of an array are read without skipping them.
    auto betweenKeyAndColon = json::Value{};
    auto first = json::Reader{};
    EXPECT_FALSE(first.parse(std::string{R"JSON({"a"/*c*/:1})JSON"}, betweenKeyAndColon));

    auto commentOnlyArray = json::Value{};
    auto second = json::Reader{};
    EXPECT_FALSE(second.parse(std::string{R"JSON([/*c*/])JSON"}, commentOnlyArray));
}

TEST(JsonReader, accepts_trailing_content_after_the_document)
{
    // Nothing checks that the document is the whole input.
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON([1,2,3] garbage)JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root.size(), 3u);
    }

    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"a":1} {"b":2})JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["a"].asInt(), 1);
        EXPECT_FALSE(root.isMember("b"));
    }
}

TEST(JsonReader, decodes_string_escapes)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"JSON({"v":"a\nb\tc\"d\\e\/f\bg\fh"})JSON"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root["v"].asString(), "a\nb\tc\"d\\e/f\bg\fh");
}

TEST(JsonReader, decodes_unicode_escapes)
{
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"v":"\u0041\u00e9\u20AC"})JSON"}, root))
            << reader.getFormattedErrorMessages();
        // U+0041 is 1 byte, U+00E9 is 2, U+20AC is 3.
        EXPECT_EQ(root["v"].asString(), "A\xC3\xA9\xE2\x82\xAC");
    }

    {
        // A surrogate pair combines into one 4-byte code point.
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"JSON({"v":"\uD83D\uDE00"})JSON"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["v"].asString(), "\xF0\x9F\x98\x80");
    }
}

TEST(JsonReader, rejects_malformed_escapes)
{
    for (auto const* document : {
             R"JSON({"v":"\x"})JSON",              // not an escape character
             R"JSON({"v":"\u00"})JSON",            // too few hex digits
             R"JSON({"v":"\uZZZZ"})JSON",          // not hexadecimal
             R"JSON({"v":"\uDC00"})JSON",          // unpaired trailing surrogate
             R"JSON({"v":"\uD800\u0041zz"})JSON",  // leading surrogate, then a non-surrogate
             R"JSON({"v":"\uD800\uD800zz"})JSON",  // two leading surrogates
             R"JSON({"v":"\uD800"})JSON",          // leading surrogate, truncated
         })
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        EXPECT_FALSE(reader.parse(std::string{document}, root)) << document;
    }
}

TEST(JsonReader, parses_empty_containers)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"JSON({"a":{},"b":[]})JSON"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_TRUE(root["a"].isObject());
    EXPECT_EQ(root["a"].size(), 0u);
    EXPECT_TRUE(root["b"].isArray());
    EXPECT_EQ(root["b"].size(), 0u);
}

TEST(JsonReader, is_movable_but_not_copyable)
{
    static_assert(!std::is_copy_constructible_v<json::Reader>);
    static_assert(!std::is_copy_assignable_v<json::Reader>);
    static_assert(std::is_nothrow_move_constructible_v<json::Reader>);
    static_assert(std::is_nothrow_move_assignable_v<json::Reader>);
}

TEST(JsonReader, a_move_constructed_reader_parses_into_its_own_target)
{
    auto sourceRoot = json::Value{};
    auto source = json::Reader{};
    ASSERT_TRUE(source.parse(std::string{R"JSON({"from":"source"})JSON"}, sourceRoot))
        << source.getFormattedErrorMessages();

    auto moved = json::Reader{std::move(source)};

    auto movedRoot = json::Value{};
    ASSERT_TRUE(moved.parse(std::string{R"JSON({"from":"moved"})JSON"}, movedRoot))
        << moved.getFormattedErrorMessages();

    EXPECT_EQ(movedRoot["from"].asString(), "moved");
    // The original's target must not have been written through.
    EXPECT_EQ(sourceRoot["from"].asString(), "source");
}

TEST(JsonReader, a_move_assigned_reader_parses_into_its_own_target)
{
    auto sourceRoot = json::Value{};
    auto source = json::Reader{};
    ASSERT_TRUE(source.parse(std::string{R"JSON({"from":"source"})JSON"}, sourceRoot))
        << source.getFormattedErrorMessages();

    auto moved = json::Reader{};
    moved = std::move(source);

    auto movedRoot = json::Value{};
    ASSERT_TRUE(moved.parse(std::string{R"JSON({"from":"moved"})JSON"}, movedRoot))
        << moved.getFormattedErrorMessages();

    EXPECT_EQ(movedRoot["from"].asString(), "moved");
    EXPECT_EQ(sourceRoot["from"].asString(), "source");
}

TEST(JsonReader, a_move_carries_error_messages_across_intact)
{
    // A short document fits in the string's inline buffer, which moving the
    // string itself would relocate. Re-parsing through the source afterwards
    // refills its buffer with newlines ahead of the recorded offset, so a
    // location left pointing at the source's buffer reports a different line.
    auto root = json::Value{};
    auto source = json::Reader{};
    ASSERT_FALSE(source.parse(std::string{R"JSON({"a":})JSON"}, root));
    ASSERT_EQ(source.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << source.getFormattedErrorMessages();

    auto const moved = json::Reader{std::move(source)};

    source = json::Reader{};
    auto reuseRoot = json::Value{};
    ASSERT_TRUE(source.parse(std::string{"\n\n\n\n[1]"}, reuseRoot))
        << source.getFormattedErrorMessages();

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonReader, a_move_assignment_carries_error_messages_across_intact)
{
    // See a_move_carries_error_messages_across_intact.
    auto root = json::Value{};
    auto source = json::Reader{};
    ASSERT_FALSE(source.parse(std::string{R"JSON({"a":})JSON"}, root));

    auto moved = json::Reader{};
    auto movedRoot = json::Value{};
    ASSERT_TRUE(moved.parse(std::string{R"JSON([1])JSON"}, movedRoot))
        << moved.getFormattedErrorMessages();
    moved = std::move(source);

    source = json::Reader{};
    auto reuseRoot = json::Value{};
    ASSERT_TRUE(source.parse(std::string{"\n\n\n\n[1]"}, reuseRoot))
        << source.getFormattedErrorMessages();

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonReader, a_move_preserves_error_locations_into_a_caller_owned_buffer)
{
    // Locations into a caller-owned buffer must be left alone.
    auto const document = std::string{R"JSON({"a":})JSON"};

    auto root = json::Value{};
    auto source = json::Reader{};
    ASSERT_FALSE(source.parse(document.data(), document.data() + document.size(), root));

    auto const moved = json::Reader{std::move(source)};

    EXPECT_EQ(moved.getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << moved.getFormattedErrorMessages();
}

TEST(JsonReader, survives_relocation_inside_a_vector)
{
    auto readers = std::vector<json::Reader>{};
    readers.reserve(1);
    readers.emplace_back();

    auto first = json::Value{};
    ASSERT_FALSE(readers.front().parse(std::string{R"JSON({"a":})JSON"}, first));

    while (readers.size() < 8)
    {
        readers.emplace_back();
    }

    EXPECT_EQ(readers.front().getFormattedErrorMessages().find("* Line 1, Column 6"), 0u)
        << readers.front().getFormattedErrorMessages();

    auto root = json::Value{};
    ASSERT_TRUE(readers.back().parse(std::string{R"JSON({"a":1})JSON"}, root))
        << readers.back().getFormattedErrorMessages();
    EXPECT_EQ(root["a"].asInt(), 1);
}

}  // namespace xrpl
