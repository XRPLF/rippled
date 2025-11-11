#include <xrpl/basics/Expected.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/TER.h>

#if BOOST_VERSION >= 107500
#include <boost/json.hpp>  // Not part of boost before version 1.75
#endif                     // BOOST_VERSION
#include <array>
#include <cstdint>

namespace ripple {
namespace test {

struct Expected_test : beast::unit_test::suite
{
    void
    run() override
    {
        // Test non-error const construction.
        {
            auto const expected = []() -> Expected<std::string, TER> {
                return "Valid value";
            }();
            BEAST_EXPECT(expected);
            BEAST_EXPECT(expected.has_value());
            BEAST_EXPECT(expected.value() == "Valid value");
            BEAST_EXPECT(*expected == "Valid value");
            BEAST_EXPECT(expected->at(0) == 'V');

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] TER const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test non-error non-const construction.
        {
            auto expected = []() -> Expected<std::string, TER> {
                return "Valid value";
            }();
            BEAST_EXPECT(expected);
            BEAST_EXPECT(expected.has_value());
            BEAST_EXPECT(expected.value() == "Valid value");
            BEAST_EXPECT(*expected == "Valid value");
            BEAST_EXPECT(expected->at(0) == 'V');
            std::string mv = std::move(*expected);
            BEAST_EXPECT(mv == "Valid value");

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] TER const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test non-error overlapping type construction.
        {
            auto expected = []() -> Expected<std::uint32_t, std::uint16_t> {
                return 1;
            }();
            BEAST_EXPECT(expected);
            BEAST_EXPECT(expected.has_value());
            BEAST_EXPECT(expected.value() == 1);
            BEAST_EXPECT(*expected == 1);

            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::uint16_t const t = expected.error();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test error construction from rvalue.
        {
            auto const expected = []() -> Expected<std::string, TER> {
                return Unexpected(telLOCAL_ERROR);
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(!expected.has_value());
            BEAST_EXPECT(expected.error() == telLOCAL_ERROR);

            bool throwOccurred = false;
            try
            {
                // There's no result, so should throw.
                [[maybe_unused]] std::string const s = *expected;
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test error construction from lvalue.
        {
            auto const err(telLOCAL_ERROR);
            auto expected = [&err]() -> Expected<std::string, TER> {
                return Unexpected(err);
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(!expected.has_value());
            BEAST_EXPECT(expected.error() == telLOCAL_ERROR);

            bool throwOccurred = false;
            try
            {
                // There's no result, so should throw.
                [[maybe_unused]] std::size_t const s = expected->size();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test error construction from const char*.
        {
            auto const expected = []() -> Expected<int, char const*> {
                return Unexpected("Not what is expected!");
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(!expected.has_value());
            BEAST_EXPECT(
                expected.error() == std::string("Not what is expected!"));
        }
        // Test error construction of string from const char*.
        {
            auto expected = []() -> Expected<int, std::string> {
                return Unexpected("Not what is expected!");
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(!expected.has_value());
            BEAST_EXPECT(expected.error() == "Not what is expected!");
            std::string const s(std::move(expected.error()));
            BEAST_EXPECT(s == "Not what is expected!");
        }
        // Test non-error const construction of Expected<void, T>.
        {
            auto const expected = []() -> Expected<void, std::string> {
                return {};
            }();
            BEAST_EXPECT(expected);
            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::size_t const s = expected.error().size();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test non-error non-const construction of Expected<void, T>.
        {
            auto expected = []() -> Expected<void, std::string> {
                return {};
            }();
            BEAST_EXPECT(expected);
            bool throwOccurred = false;
            try
            {
                // There's no error, so should throw.
                [[maybe_unused]] std::size_t const s = expected.error().size();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == std::string("bad expected access"));
                throwOccurred = true;
            }
            BEAST_EXPECT(throwOccurred);
        }
        // Test error const construction of Expected<void, T>.
        {
            auto const expected = []() -> Expected<void, std::string> {
                return Unexpected("Not what is expected!");
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(expected.error() == "Not what is expected!");
        }
        // Test error non-const construction of Expected<void, T>.
        {
            auto expected = []() -> Expected<void, std::string> {
                return Unexpected("Not what is expected!");
            }();
            BEAST_EXPECT(!expected);
            BEAST_EXPECT(expected.error() == "Not what is expected!");
            std::string const s(std::move(expected.error()));
            BEAST_EXPECT(s == "Not what is expected!");
        }
        // Test a case that previously unintentionally returned an array.
#if BOOST_VERSION >= 107500
        {
            auto expected = []() -> Expected<boost::json::value, std::string> {
                return boost::json::object{{"oops", "me array now"}};
            }();
            BEAST_EXPECT(expected);
            BEAST_EXPECT(!expected.value().is_array());
        }
#endif  // BOOST_VERSION
    }
};

BEAST_DEFINE_TESTSUITE(Expected, basics, ripple);

}  // namespace test
}  // namespace ripple
