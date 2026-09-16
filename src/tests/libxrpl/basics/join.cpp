#include <xrpl/basics/join.h>

#include <xrpl/basics/base_uint.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl::test {

struct JoinTest : public ::testing::Test
{
};

TEST_F(JoinTest, join)
{
    auto test = [](auto collectionAndDelimiter, std::string expected) {
        std::stringstream ss;
        // Put something else in the buffer before and after to ensure that
        // the << operator returns the stream correctly.
        ss << "(" << collectionAndDelimiter << ")";
        auto const str = ss.str();
        EXPECT_EQ(str.substr(1, str.length() - 2), expected);
        EXPECT_EQ(str.front(), '(');
        EXPECT_EQ(str.back(), ')');
    };

    // C++ array
    test(CollectionAndDelimiter(std::array<int, 4>{2, -1, 5, 10}, "/"), "2/-1/5/10");
    // One item C++ array edge case
    test(CollectionAndDelimiter(std::array<std::string, 1>{"test"}, " & "), "test");
    // Empty C++ array edge case
    test(CollectionAndDelimiter(std::array<int, 0>{}, ","), "");
    {
        // C-style array
        char letters[4]{'w', 'a', 's', 'd'};
        test(CollectionAndDelimiter(letters, std::to_string(0)), "w0a0s0d");
    }
    {
        // Auto sized C-style array
        std::string words[]{"one", "two", "three", "four"};
        test(CollectionAndDelimiter(words, "\n"), "one\ntwo\nthree\nfour");
    }
    {
        // One item C-style array edge case
        std::string words[]{"thing"};
        test(CollectionAndDelimiter(words, "\n"), "thing");
    }
    // Initializer list
    test(CollectionAndDelimiter(std::initializer_list<size_t>{19, 25}, "+"), "19+25");
    // vector
    test(CollectionAndDelimiter(std::vector<int>{0, 42}, std::to_string(99)), "09942");
    // vector with one item edge case
    test(CollectionAndDelimiter(std::vector<std::string>{"master"}, "xxx"), "master");
    // vector with one non-trivial streamable item edge case
    test(
        CollectionAndDelimiter(std::vector<uint256>{uint256{1}}, "xxx"),
        "0000000000000000000000000000000000000000000000000000000000000001");
    // empty vector edge case
    test(CollectionAndDelimiter(std::vector<uint256>{}, ","), "");
    // C-style string
    test(CollectionAndDelimiter("string", " "), "s t r i n g");
    // Empty C-style string edge case
    test(CollectionAndDelimiter("", "*"), "");
    // Single char C-style string edge case
    test(CollectionAndDelimiter("x", "*"), "x");
    // std::string
    test(CollectionAndDelimiter(std::string{"string"}, "-"), "s-t-r-i-n-g");
    // Empty std::string edge case
    test(CollectionAndDelimiter(std::string{""}, "*"), "");
    // Single char std::string edge case
    test(CollectionAndDelimiter(std::string{"y"}, "*"), "y");
}

}  // namespace xrpl::test
