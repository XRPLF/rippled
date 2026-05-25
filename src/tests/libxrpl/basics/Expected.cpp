#include <xrpl/basics/Expected.h>

#include <xrpl/protocol/TER.h>

#include <boost/json/value.hpp>
#include <boost/version.hpp>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#if BOOST_VERSION >= 107500
#endif  // BOOST_VERSION
#include <cstdint>

namespace xrpl::test {

TEST(ExpectedTest, non_error_const_construction)
{
    auto const expected = []() -> Expected<std::string, TER> { return "Valid value"; }();
    EXPECT_TRUE(expected);
    EXPECT_TRUE(expected.has_value());
    EXPECT_EQ(expected.value(), "Valid value");
    EXPECT_EQ(*expected, "Valid value");
    EXPECT_EQ(expected->at(0), 'V');

    EXPECT_THAT(
        [&expected] { [[maybe_unused]] TER const t = expected.error(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, non_error_non_const_construction)
{
    auto expected = []() -> Expected<std::string, TER> { return "Valid value"; }();
    EXPECT_TRUE(expected);
    EXPECT_TRUE(expected.has_value());
    EXPECT_EQ(expected.value(), "Valid value");
    EXPECT_EQ(*expected, "Valid value");
    EXPECT_EQ(expected->at(0), 'V');
    std::string const mv = std::move(*expected);
    EXPECT_EQ(mv, "Valid value");

    EXPECT_THAT(
        [&expected] { [[maybe_unused]] TER const t = expected.error(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, non_error_overlapping_type_construction)
{
    auto expected = []() -> Expected<std::uint32_t, std::uint16_t> { return 1; }();
    EXPECT_TRUE(expected);
    EXPECT_TRUE(expected.has_value());
    EXPECT_EQ(expected.value(), 1);
    EXPECT_EQ(*expected, 1);
    EXPECT_THAT(
        [&expected] { [[maybe_unused]] std::uint16_t const t = expected.error(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, error_construction_from_rvalue)
{
    auto const expected = []() -> Expected<std::string, TER> {
        return Unexpected(telLOCAL_ERROR);
    }();
    EXPECT_TRUE(!expected);
    EXPECT_TRUE(!expected.has_value());
    EXPECT_EQ(expected.error(), telLOCAL_ERROR);

    EXPECT_THAT(
        [&expected] { [[maybe_unused]] std::string const s = *expected; },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, error_construction_from_lvalue)
{
    auto const err(telLOCAL_ERROR);
    auto expected = [&err]() -> Expected<std::string, TER> { return Unexpected(err); }();
    EXPECT_TRUE(!expected);
    EXPECT_TRUE(!expected.has_value());
    EXPECT_EQ(expected.error(), telLOCAL_ERROR);
    EXPECT_THAT(
        [&expected] { [[maybe_unused]] std::size_t const s = expected->size(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, error_construction_from_const_char)
{
    auto const expected = []() -> Expected<int, char const*> {
        return Unexpected("Not what is expected!");
    }();
    EXPECT_TRUE(!expected);
    EXPECT_TRUE(!expected.has_value());
    EXPECT_EQ(expected.error(), std::string("Not what is expected!"));
}

TEST(ExpectedTest, error_string_construction_from_const_char)
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

TEST(ExpectedTest, non_error_const_void_construction)
{
    auto const expected = []() -> Expected<void, std::string> { return {}; }();
    EXPECT_TRUE(expected);

    EXPECT_THAT(
        [&expected] { [[maybe_unused]] std::size_t const s = expected.error().size(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, non_error_non_const_void_construction)
{
    auto expected = []() -> Expected<void, std::string> { return {}; }();
    EXPECT_TRUE(expected);

    EXPECT_THAT(
        [&expected] { [[maybe_unused]] std::size_t const s = expected.error().size(); },
        ::testing::ThrowsMessage<std::runtime_error>("bad expected access"));
}

TEST(ExpectedTest, error_const_void_construction)
{
    auto const expected = []() -> Expected<void, std::string> {
        return Unexpected("Not what is expected!");
    }();
    EXPECT_TRUE(!expected);
    EXPECT_EQ(expected.error(), "Not what is expected!");
}

TEST(ExpectedTest, error_non_const_void_construction)
{
    auto expected = []() -> Expected<void, std::string> {
        return Unexpected("Not what is expected!");
    }();
    EXPECT_TRUE(!expected);
    EXPECT_EQ(expected.error(), "Not what is expected!");
    std::string const s(std::move(expected.error()));
    EXPECT_EQ(s, "Not what is expected!");
}

#if BOOST_VERSION >= 107500
TEST(ExpectedTest, json_object_value_construction)
{
    auto expected = []() -> Expected<boost::json::value, std::string> {
        return boost::json::object{{"oops", "me array now"}};
    }();
    EXPECT_TRUE(expected);
    EXPECT_TRUE(!expected.value().is_array());
}
#endif  // BOOST_VERSION

}  // namespace xrpl::test
