#include <xrpl/json/Loggable.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/MaskSecrets.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>

#include <fstream>
#include <ios>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace xrpl {

namespace {

constexpr char const* kMasked = "<masked>";
constexpr char const* kSensitive = "sensitive";

/**
 * Whether a `T` can be written to an `S` with `<<`.
 */
template <class S, class T>
concept Streamable = requires(S& stream, T const& value) { stream << value; };

using Stream = beast::Journal::Stream;
using ScopedStream = beast::Journal::ScopedStream const;

// A raw value does not go into a log statement, at the start or later in the chain.
static_assert(!Streamable<Stream, json::Value>);
static_assert(!Streamable<ScopedStream, json::Value>);
static_assert(!Streamable<Stream, json::Compact>);
static_assert(!Streamable<ScopedStream, json::Compact>);

// The wrappers do, and other operands are not affected.
static_assert(Streamable<Stream, json::Masked>);
static_assert(Streamable<ScopedStream, json::Masked>);
static_assert(Streamable<Stream, json::Unmasked>);
static_assert(Streamable<ScopedStream, json::Unmasked>);
static_assert(Streamable<Stream, std::string>);
static_assert(Streamable<ScopedStream, json::StaticString>);

// The wrappers are for the journal only, and a raw value still goes to a std::ostream.
static_assert(!Streamable<std::ostream, json::Masked>);
static_assert(!Streamable<std::ostream, json::Unmasked>);
static_assert(Streamable<std::ostream, json::Value>);
static_assert(Streamable<std::ostream, json::Compact>);

// A wrapper lives in the log statement that builds it.
static_assert(!std::is_copy_constructible_v<json::Masked>);
static_assert(!std::is_move_constructible_v<json::Masked>);
static_assert(!std::is_copy_assignable_v<json::Masked>);
static_assert(!std::is_copy_constructible_v<json::Unmasked>);
static_assert(!std::is_move_constructible_v<json::Unmasked>);
static_assert(!std::is_copy_assignable_v<json::Unmasked>);

/**
 * Builds a request with a credential under an object inside an array.
 *
 * @return The request.
 */
[[nodiscard]] json::Value
nestedSecret()
{
    json::Value params(json::ValueType::Object);
    params[jss::secret] = kSensitive;
    params[jss::account] = "rSomeAccount";

    json::Value request(json::ValueType::Object);
    request[jss::method] = "sign";
    request[jss::params] = json::ValueType::Array;
    request[jss::params][0u] = params;
    return request;
}

}  // namespace

TEST(Loggable, masked_masks_at_depth)
{
    auto const request = nestedSecret();

    // As the first operand, which goes to Journal::Stream.
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << json::Masked{request};

        auto const text = sink.messages();
        EXPECT_FALSE(text.contains(kSensitive));
        EXPECT_TRUE(text.contains(kMasked));
        EXPECT_TRUE(text.contains("rSomeAccount"));
    }

    // As a later operand, which goes to Journal::ScopedStream.
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << "request: " << json::Masked{request} << " end";

        auto const text = sink.messages();
        EXPECT_TRUE(text.starts_with("request: {"));
        EXPECT_TRUE(text.ends_with(" end\n"));
        EXPECT_FALSE(text.contains(kSensitive));
        EXPECT_TRUE(text.contains(kMasked));
    }

    // The value itself is not changed.
    EXPECT_EQ(request[jss::params][0u][jss::secret], kSensitive);
}

TEST(Loggable, masked_passes_a_value_without_credential_through)
{
    json::Value request(json::ValueType::Object);
    request[jss::method] = "ledger";
    request[jss::ledger_index] = 5;

    test::CaptureSink sink;
    beast::Journal const j{sink};
    JLOG(j.debug()) << json::Masked{request};
    JLOG(j.debug()) << "again " << json::Masked{request};

    EXPECT_EQ(sink.messages(), to_string(request) + "\nagain " + to_string(request) + "\n");
}

TEST(Loggable, unmasked_leaves_a_credential_name_visible)
{
    auto const request = nestedSecret();

    test::CaptureSink sink;
    beast::Journal const j{sink};
    JLOG(j.debug()) << json::Unmasked{request};
    JLOG(j.debug()) << "again " << json::Unmasked{request};

    EXPECT_EQ(sink.messages(), to_string(request) + "\nagain " + to_string(request) + "\n");
    EXPECT_TRUE(sink.messages().contains(kSensitive));
}

TEST(Loggable, both_wrappers_truncate_to_the_cap)
{
    json::Value const big{std::string(2 * json::kMaxLoggedChars, 'x')};
    constexpr std::string_view kPrefix = "big: ";

    // The sink ends each message with a newline.
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << json::Masked{big};
        EXPECT_EQ(sink.messages().size(), json::kMaxLoggedChars + 1);
    }
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << kPrefix << json::Masked{big};
        EXPECT_EQ(sink.messages().size(), kPrefix.size() + json::kMaxLoggedChars + 1);
    }
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << json::Unmasked{big};
        EXPECT_EQ(sink.messages().size(), json::kMaxLoggedChars + 1);
    }
    {
        test::CaptureSink sink;
        beast::Journal const j{sink};
        JLOG(j.debug()) << kPrefix << json::Unmasked{big};
        EXPECT_EQ(sink.messages().size(), kPrefix.size() + json::kMaxLoggedChars + 1);
    }
}

TEST(Loggable, value_over_the_cap_gives_a_line_of_the_line_limit)
{
    TempDir const dir;
    auto const file = dir.file("debug.log");

    json::Value const big{std::string(2 * json::kMaxLoggedChars, 'x')};
    {
        Logs logs{beast::Severity::Debug};
        logs.silent(true);
        ASSERT_TRUE(logs.open(file));

        auto const j = logs.journal("Test");
        JLOG(j.debug()) << json::Masked{big};
        JLOG(j.debug()) << json::Unmasked{big};
    }

    std::ifstream input{file};
    std::string line;
    for (int count = 0; count < 2; ++count)
    {
        ASSERT_TRUE(std::getline(input, line));
        EXPECT_EQ(line.size(), Logs::kMaximumMessageCharacters);
        EXPECT_TRUE(line.ends_with("x..."));
    }
    EXPECT_FALSE(std::getline(input, line));
}

TEST(Loggable, inactive_sink_gets_nothing)
{
    auto const request = nestedSecret();

    test::CaptureSink sink{beast::Severity::Warning};
    beast::Journal const j{sink};
    JLOG(j.debug()) << json::Masked{request};
    JLOG(j.debug()) << "request: " << json::Unmasked{request};

    EXPECT_TRUE(sink.messages().empty());
}

TEST(Loggable, chain_keeps_its_type_after_other_operands)
{
    auto const request = nestedSecret();

    test::CaptureSink sink;
    beast::Journal const j{sink};
    JLOG(j.debug()) << "a " << 1 << ' ' << std::hex << 255 << ' ' << json::Masked{request}
                    << std::endl;

    auto const text = sink.messages();
    EXPECT_TRUE(text.starts_with("a 1 0xff {"));
    EXPECT_FALSE(text.contains(kSensitive));
}

}  // namespace xrpl
