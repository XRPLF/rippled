#include <xrpl/basics/Expected.h>

#include <xrpl/protocol/TER.h>

#include <boost/json/value.hpp>
#include <boost/version.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#if BOOST_VERSION >= 107500
#endif  // BOOST_VERSION
#include <cstdint>

namespace xrpl::test {

struct ExpectedTest : public ::testing::Test
{
    static void
    run()
    {
        // Test non-error const construction.
        {
            auto const expected = []() -> Expected<std::string, TER> { return "Valid value"; }();
            EXPECT_TRUE(expected);
            EXPECT_TRUE(expected.has_value());
            EXPECT_EQ(expected.value(), "Valid value");
            EXPECT_EQ(*expected, "Valid value");
            EXPECT_EQ(expected->at(0), 'V');

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] TER const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test non-error non-const construction.
        {
            auto expected = []() -> Expected<std::string, TER> { return "Valid value"; }();
            EXPECT_TRUE(expected);
            EXPECT_TRUE(expected.has_value());
            EXPECT_EQ(expected.value(), "Valid value");
            EXPECT_EQ(*expected, "Valid value");
            EXPECT_EQ(expected->at(0), 'V');
            std::string const mv = std::move(*expected);
            EXPECT_EQ(mv, "Valid value");

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] TER const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test non-error overlapping type construction.
        {
            auto expected = []() -> Expected<std::uint32_t, std::uint16_t> { return 1; }();
            EXPECT_TRUE(expected);
            EXPECT_TRUE(expected.has_value());
            EXPECT_EQ(expected.value(), 1);
            EXPECT_EQ(*expected, 1);

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::uint16_t const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test error construction from rvalue.
        {
            auto const expected = []() -> Expected<std::string, TER> {
                return Unexpected(telLOCAL_ERROR);
            }();
            EXPECT_TRUE(!expected);
            EXPECT_TRUE(!expected.has_value());
            EXPECT_EQ(expected.error(), telLOCAL_ERROR);

            bool throwOccurred = false;
            try
            {
                // There's no result, so should throw.
                [[maybe_unused]] std::string const s = *expected;
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test error construction from lvalue.
        {
            auto const err(telLOCAL_ERROR);
            auto expected = [&err]() -> Expected<std::string, TER> { return Unexpected(err); }();
            EXPECT_TRUE(!expected);
            EXPECT_TRUE(!expected.has_value());
            EXPECT_EQ(expected.error(), telLOCAL_ERROR);

            bool throwOccurred = false;
            try
            {
                // There's no result, so should throw.
                [[maybe_unused]] std::size_t const s = expected->size();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test error construction from const char*.
        {
            auto const expected = []() -> Expected<int, char const*> {
                return Unexpected("Not what is expected!");
            }();
            EXPECT_TRUE(!expected);
            EXPECT_TRUE(!expected.has_value());
            EXPECT_EQ(expected.error(), std::string("Not what is expected!"));
        }
        // Test error construction of string from const char*.
        {
            auto expected = []() -> Expected<int, std::string> {
                return Unexpected("Not what is expected!");
            }();
            EXPECT_TRUE(!expected);
            EXPECT_TRUE(!expected.has_value());
            EXPECT_EQ(expected.error(), "Not what is expected!");
            std::string const s(std::move(expected.error()));
            EXPECT_EQ(s, "Not what is expected!");
        }
        // Test non-error const construction of Expected<void, T>.
        {
            auto const expected = []() -> Expected<void, std::string> { return {}; }();
            EXPECT_TRUE(expected);
            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::size_t const s = expected.error().size();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test non-error non-const construction of Expected<void, T>.
        {
            auto expected = []() -> Expected<void, std::string> { return {}; }();
            EXPECT_TRUE(expected);
            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::size_t const s = expected.error().size();
            }
            catch (std::runtime_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("bad expected access"));
                throwOccurred = true;
            }
            EXPECT_TRUE(throwOccurred);
        }
        // Test error const construction of Expected<void, T>.
        {
            auto const expected = []() -> Expected<void, std::string> {
                return Unexpected("Not what is expected!");
            }();
            EXPECT_TRUE(!expected);
            EXPECT_EQ(expected.error(), "Not what is expected!");
        }
        // Test error non-const construction of Expected<void, T>.
        {
            auto expected = []() -> Expected<void, std::string> {
                return Unexpected("Not what is expected!");
            }();
            EXPECT_TRUE(!expected);
            EXPECT_EQ(expected.error(), "Not what is expected!");
            std::string const s(std::move(expected.error()));
            EXPECT_EQ(s, "Not what is expected!");
        }
        // Test a case that previously unintentionally returned an array.
#if BOOST_VERSION >= 107500
        {
            auto expected = []() -> Expected<boost::json::value, std::string> {
                return boost::json::object{{"oops", "me array now"}};
            }();
            EXPECT_TRUE(expected);
            EXPECT_TRUE(!expected.value().is_array());
        }
#endif  // BOOST_VERSION
    }
};

TEST_F(ExpectedTest, expected)
{
    run();
}

}  // namespace xrpl::test
