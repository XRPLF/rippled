#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace xrpl {

// Reader delegates tokenizing to json::Parser and keeps only the policy that
// belongs to the json::Value representation. These tests cover that policy,
// plus the tree it builds.

TEST(JsonReader, rejects_duplicate_keys)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    EXPECT_FALSE(reader.parse(std::string{R"({"a":1,"a":2})"}, root));
    EXPECT_NE(reader.getFormattedErrorMessages().find("appears twice"), std::string::npos)
        << reader.getFormattedErrorMessages();
}

TEST(JsonReader, rejects_duplicate_keys_in_a_nested_object)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    EXPECT_FALSE(reader.parse(std::string{R"({"outer":{"a":1,"a":2}})"}, root));
}

TEST(JsonReader, allows_the_same_key_in_sibling_objects)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"({"x":{"a":1},"y":{"a":2}})"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root["x"]["a"].asInt(), 1);
    EXPECT_EQ(root["y"]["a"].asInt(), 2);
}

TEST(JsonReader, requires_an_object_array_or_null_document)
{
    for (auto const* scalar : {R"("a string")", "42", "2.5", "true", "false"})
    {
        auto root = json::Value{};
        auto reader = json::Reader{};

        EXPECT_FALSE(reader.parse(std::string{scalar}, root)) << scalar;
        EXPECT_NE(
            reader.getFormattedErrorMessages().find("must be either an array or an object"),
            std::string::npos)
            << scalar;
    }

    for (auto const* document : {"{}", "[]", "null", R"({"a":1})", "[1,2]"})
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

    ASSERT_TRUE(reader.parse(std::string{R"({"b":[1,2.5,true,null],"a":"x"})"}, root))
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

    ASSERT_TRUE(reader.parse(std::string{R"({"z":1,"m":2,"a":3})"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root.getMemberNames(), (std::vector<std::string>{"a", "m", "z"}));
}

TEST(JsonReader, is_reusable_across_parses)
{
    auto reader = json::Reader{};

    {
        auto root = json::Value{};
        ASSERT_TRUE(reader.parse(std::string{R"({"a":1})"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["a"].asInt(), 1);
    }

    {
        // A failure must not leak errors into the next parse.
        auto root = json::Value{};
        EXPECT_FALSE(reader.parse(std::string{R"({"a":})"}, root));
        EXPECT_FALSE(reader.getFormattedErrorMessages().empty());
    }

    {
        auto root = json::Value{};
        ASSERT_TRUE(reader.parse(std::string{R"({"b":[2]})"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["b"][0u].asInt(), 2);
        EXPECT_FALSE(root.isMember("a"));
        EXPECT_TRUE(reader.getFormattedErrorMessages().empty());
    }
}

TEST(JsonReader, parses_from_a_character_range)
{
    auto const document = std::string{R"({"a":1})"};

    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(document.data(), document.data() + document.size(), root))
        << reader.getFormattedErrorMessages();
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, parses_from_a_stream)
{
    auto input = std::istringstream{R"({"a":1})"};

    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(input, root)) << reader.getFormattedErrorMessages();
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, parses_a_buffer_sequence_from_a_temporary_reader)
{
    // ServerHandler parses request bodies this way: a temporary Reader over a
    // multi-fragment buffer sequence.
    auto const head = std::string{R"({"a":)"};
    auto const tail = std::string{R"(1})"};

    auto const buffers = std::vector<boost::asio::const_buffer>{
        boost::asio::const_buffer{head.data(), head.size()},
        boost::asio::const_buffer{tail.data(), tail.size()}};

    auto root = json::Value{};
    ASSERT_TRUE(json::Reader{}.parse(root, buffers));
    EXPECT_TRUE(root.isObject());
    EXPECT_EQ(root["a"].asInt(), 1);
}

TEST(JsonReader, stream_extraction_throws_on_bad_input)
{
    auto root = json::Value{};
    auto good = std::istringstream{R"({"a":1})"};
    EXPECT_NO_THROW(good >> root);
    EXPECT_EQ(root["a"].asInt(), 1);

    auto bad = std::istringstream{R"({"a":)"};
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
        s += R"("o":{)";
    }
    for (unsigned i = 0; i < depth; ++i)
    {
        s += "}";
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
             R"({/*c*/"a":1})",
             R"({"a":/*c*/1})",
             R"({"a":1/*c*/})",
             R"({"a":1} /* trailing */)",
             R"({"a":1} // trailing)",
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
        ASSERT_TRUE(reader.parse(std::string{R"({"a":1,/*c*/"b":2})"}, root))
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
    EXPECT_FALSE(first.parse(std::string{R"({"a"/*c*/:1})"}, betweenKeyAndColon));

    auto commentOnlyArray = json::Value{};
    auto second = json::Reader{};
    EXPECT_FALSE(second.parse(std::string{"[/*c*/]"}, commentOnlyArray));
}

TEST(JsonReader, accepts_trailing_content_after_the_document)
{
    // Nothing checks that the document is the whole input.
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{"[1,2,3] garbage"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root.size(), 3u);
    }

    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"({"a":1} {"b":2})"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["a"].asInt(), 1);
        EXPECT_FALSE(root.isMember("b"));
    }
}

TEST(JsonReader, decodes_string_escapes)
{
    auto root = json::Value{};
    auto reader = json::Reader{};

    ASSERT_TRUE(reader.parse(std::string{R"({"v":"a\nb\tc\"d\\e\/f\bg\fh"})"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_EQ(root["v"].asString(), "a\nb\tc\"d\\e/f\bg\fh");
}

TEST(JsonReader, decodes_unicode_escapes)
{
    {
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"({"v":"\u0041\u00e9\u20AC"})"}, root))
            << reader.getFormattedErrorMessages();
        // U+0041 is 1 byte, U+00E9 is 2, U+20AC is 3.
        EXPECT_EQ(root["v"].asString(), "A\xC3\xA9\xE2\x82\xAC");
    }

    {
        // A surrogate pair combines into one 4-byte code point.
        auto root = json::Value{};
        auto reader = json::Reader{};
        ASSERT_TRUE(reader.parse(std::string{R"({"v":"\uD83D\uDE00"})"}, root))
            << reader.getFormattedErrorMessages();
        EXPECT_EQ(root["v"].asString(), "\xF0\x9F\x98\x80");
    }
}

TEST(JsonReader, rejects_malformed_escapes)
{
    for (auto const* document : {
             R"({"v":"\x"})",              // not an escape character
             R"({"v":"\u00"})",            // too few hex digits
             R"({"v":"\uZZZZ"})",          // not hexadecimal
             R"({"v":"\uDC00"})",          // unpaired trailing surrogate
             R"({"v":"\uD800\u0041zz"})",  // leading surrogate, then a non-surrogate
             R"({"v":"\uD800\uD800zz"})",  // two leading surrogates
             R"({"v":"\uD800"})",          // leading surrogate, truncated
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

    ASSERT_TRUE(reader.parse(std::string{R"({"a":{},"b":[]})"}, root))
        << reader.getFormattedErrorMessages();

    EXPECT_TRUE(root["a"].isObject());
    EXPECT_EQ(root["a"].size(), 0u);
    EXPECT_TRUE(root["b"].isArray());
    EXPECT_EQ(root["b"].size(), 0u);
}

}  // namespace xrpl
