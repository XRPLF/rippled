#include <test/jtx/Account.h>

#include <xrpl/basics/join.h>
#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

struct join_test : beast::unit_test::suite
{
    void
    run() override
    {
        auto test = [this](auto collectionanddelimiter, std::string expected) {
            std::stringstream ss;
            // Put something else in the buffer before and after to ensure that
            // the << operator returns the stream correctly.
            ss << "(" << collectionanddelimiter << ")";
            auto const str = ss.str();
            BEAST_EXPECT(str.substr(1, str.length() - 2) == expected);
            BEAST_EXPECT(str.front() == '(');
            BEAST_EXPECT(str.back() == ')');
        };

        // C++ array
        test(
            CollectionAndDelimiter(std::array<int, 4>{2, -1, 5, 10}, "/"),
            "2/-1/5/10");
        // One item C++ array edge case
        test(
            CollectionAndDelimiter(std::array<std::string, 1>{"test"}, " & "),
            "test");
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
        test(
            CollectionAndDelimiter(std::initializer_list<size_t>{19, 25}, "+"),
            "19+25");
        // vector
        test(
            CollectionAndDelimiter(std::vector<int>{0, 42}, std::to_string(99)),
            "09942");
        {
            // vector with one item edge case
            using namespace jtx;
            test(
                CollectionAndDelimiter(
                    std::vector<Account>{Account::master}, "xxx"),
                Account::master.human());
        }
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
};  // namespace test

BEAST_DEFINE_TESTSUITE(join, basics, ripple);

}  // namespace test
}  // namespace ripple
