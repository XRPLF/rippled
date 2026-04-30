#include <xrpl/basics/Logger.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <regex>
#include <sstream>
#include <string>

using namespace xrpl;

class LoggerFixture : public ::testing::Test
{
protected:
    std::ostringstream output_;

    void
    initLogging(bool jsonMode, std::string const& pattern = "%v", Severity severity = Severity::TRC)
    {
        LoggingConfiguration config{
            .enableConsole = false,
            .directory = std::nullopt,
            .isAsync = false,
            .defaultSeverity = severity,
            .jsonMode = jsonMode,
        };
        auto const result = LogService::init(config);
        ASSERT_TRUE(result);

        // Replace the (empty) sinks with our ostream sink so we can
        // capture output in tests.  The `pattern` parameter controls
        // the test sink's formatter independently of LogService's format.
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output_);
        sink->set_level(spdlog::level::trace);
        sink->set_formatter(LogServiceState::makeFormatter(pattern));
        LogServiceState::replaceSinks({sink});
    }

    void
    TearDown() override
    {
        spdlog::drop_all();
        if (LogService::initialized())
            LogService::reset();
    }
};

// -- Plain text mode ---------------------------------------------------------

TEST_F(LoggerFixture, plain_text_simple_message)
{
    initLogging(false);
    Logger logger("TestChannel");
    logger.info() << "hello world";
    EXPECT_EQ(output_.str(), "hello world\n");
}

TEST_F(LoggerFixture, plain_text_multiple_values)
{
    initLogging(false);
    Logger logger("TestChannel2");
    logger.info() << "count=" << 42 << " active=" << true;
    EXPECT_EQ(output_.str(), "count=42 active=true\n");
}

TEST_F(LoggerFixture, plain_text_with_parameter)
{
    initLogging(false);
    Logger logger("TestChannel3");
    logger.info() << "tx " << log::param("hash", "ABC");
    // In plain text mode, parameter value is just streamed
    EXPECT_EQ(output_.str(), "tx ABC\n");
}

// -- JSON mode ---------------------------------------------------------------

TEST_F(LoggerFixture, json_mode_simple_message)
{
    initLogging(true);
    Logger logger("JsonChannel");
    logger.info() << "hello world";
    // In JSON mode, message is quoted
    EXPECT_EQ(output_.str(), "\"hello world\"\n");
}

TEST_F(LoggerFixture, json_mode_with_parameters)
{
    initLogging(true);
    Logger logger("JsonChannel2");
    logger.info() << "processing " << log::param("tx_hash", std::string("ABC123"))
                  << " amount=" << log::param("amount", 42);
    // Message body is quoted, then values object appended
    EXPECT_EQ(
        output_.str(),
        "\"processing ABC123 amount=42\", "
        "\"values\": {\"tx_hash\":\"ABC123\",\"amount\":42}\n");
}

TEST_F(LoggerFixture, json_mode_no_parameters)
{
    initLogging(true);
    Logger logger("JsonChannel3");
    logger.info() << "simple message";
    // No values object when no parameters
    EXPECT_EQ(output_.str(), "\"simple message\"\n");
}

TEST_F(LoggerFixture, json_mode_with_pattern)
{
    std::string pattern =
        JsonLoggingPatternBuilder().add("level", "%l").add("channel", "%n").build();
    initLogging(true, pattern);
    Logger logger("Overlay");
    logger.info() << "peer connected";
    EXPECT_EQ(
        output_.str(),
        "{\"level\":\"info\",\"channel\":\"Overlay\","
        " \"message\": \"peer connected\" }\n");
}

TEST_F(LoggerFixture, json_mode_bool_parameter)
{
    initLogging(true);
    Logger logger("JsonChannel4");
    logger.info() << "status " << log::param("active", true);
    EXPECT_EQ(output_.str(), "\"status true\", \"values\": {\"active\":true}\n");
}

// -- Severity filtering ------------------------------------------------------

TEST_F(LoggerFixture, severity_filtering)
{
    initLogging(false, "%v", Severity::WRN);

    Logger logger("FilterChannel");
    logger.info() << "should not appear";
    EXPECT_TRUE(output_.str().empty());

    logger.warn() << "should appear";
    EXPECT_EQ(output_.str(), "should appear\n");
}

// -- xrpl::to_string integration --------------------------------------------

TEST_F(LoggerFixture, text_mode_xrp_amount)
{
    initLogging(false);
    Logger logger("AmountChannel");
    logger.info() << "balance: " << XRPAmount{1000};
    EXPECT_EQ(output_.str(), "balance: 1000\n");
}

TEST_F(LoggerFixture, json_mode_xrp_amount)
{
    initLogging(true);
    Logger logger("AmountChannel");
    logger.info() << "balance " << XRPAmount{500};
    EXPECT_EQ(output_.str(), "\"balance 500\"\n");
}

TEST_F(LoggerFixture, json_mode_xrp_amount_parameter)
{
    initLogging(true);
    Logger logger("AmountChannel");
    logger.info() << "tx" << log::param("fee", XRPAmount{10});
    EXPECT_EQ(output_.str(), "\"tx10\", \"values\": {\"fee\":\"10\"}\n");
}

TEST_F(LoggerFixture, text_mode_number)
{
    initLogging(false);
    Logger logger("NumberChannel");
    logger.info() << "result: " << Number{42};
    EXPECT_EQ(output_.str(), "result: 42\n");
}

TEST_F(LoggerFixture, json_mode_number)
{
    initLogging(true);
    Logger logger("NumberChannel");
    logger.info() << "value " << Number{25, -3};
    EXPECT_EQ(output_.str(), "\"value 0.025\"\n");
}

TEST_F(LoggerFixture, json_mode_number_parameter)
{
    initLogging(true);
    Logger logger("NumberChannel");
    logger.info() << "calc" << log::param("rate", Number{100});
    EXPECT_EQ(output_.str(), "\"calc100\", \"values\": {\"rate\":\"100\"}\n");
}

// -- Legacy format matching ---------------------------------------------------

TEST_F(LoggerFixture, default_format_matches_legacy)
{
    // Use the default format (kDEFAULT_LOG_FORMAT) which includes %K for severity
    initLogging(false, kDEFAULT_LOG_FORMAT);
    Logger logger("General");
    logger.info() << "hello world";
    auto const line = output_.str();

    // The full output must be:
    //   YYYY-Mon-DD HH:MM:SS.ffffff UTC General:NFO hello world\n
    std::regex const expected(
        R"(\d{4}-[A-Z][a-z]{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} UTC General:NFO hello world\n)");
    EXPECT_TRUE(std::regex_match(line, expected)) << "got: " << line;
}

TEST_F(LoggerFixture, severity_codes_in_default_format)
{
    initLogging(false, "%n:%K %v");
    Logger logger("Test");

    logger.trace() << "t";
    logger.debug() << "d";
    logger.info() << "i";
    logger.warn() << "w";
    logger.error() << "e";
    logger.fatal() << "f";

    EXPECT_EQ(
        output_.str(),
        "Test:TRC t\n"
        "Test:DBG d\n"
        "Test:NFO i\n"
        "Test:WRN w\n"
        "Test:ERR e\n"
        "Test:FTL f\n");
}

TEST_F(LoggerFixture, default_json_format)
{
    // JSON mode should produce a JSON object with timestamp, channel,
    // severity and message fields.
    initLogging(true, defaultJsonLogFormat());
    Logger logger("General");
    logger.info() << "hello world";
    auto const line = output_.str();

    std::regex const expected(
        R"(\{"timestamp":"\d{4}-[A-Z][a-z]{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} UTC")"
        R"(,"channel":"General","severity":"NFO")"
        R"(, "message": "hello world" \}\n)");
    EXPECT_TRUE(std::regex_match(line, expected)) << "got: " << line;
}

// -- Secret scrubbing ---------------------------------------------------------

TEST_F(LoggerFixture, scrubs_seed)
{
    initLogging(false);
    Logger logger("Scrub");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // 31 chars in the seed value → 31 asterisks
    EXPECT_EQ(output_.str(), "{\"seed\":\"*******************************\"}\n");
}

TEST_F(LoggerFixture, scrubs_master_key)
{
    initLogging(false);
    Logger logger("Scrub2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    EXPECT_EQ(output_.str(), "{\"master_key\":\"*****************\"}\n");
}

TEST_F(LoggerFixture, scrubs_passphrase)
{
    initLogging(false);
    Logger logger("Scrub3");
    logger.info() << R"({"passphrase":"my_secret_pass"})";
    EXPECT_EQ(output_.str(), "{\"passphrase\":\"**************\"}\n");
}

TEST_F(LoggerFixture, scrubs_seed_json_mode)
{
    initLogging(true);
    Logger logger("ScrubJson");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // In JSON mode the message is wrapped in quotes, but scrubbing still works
    EXPECT_EQ(output_.str(), "\"{\"seed\":\"*******************************\"}\"\n");
}

TEST_F(LoggerFixture, scrubs_master_key_json_mode)
{
    initLogging(true);
    Logger logger("ScrubJson2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    EXPECT_EQ(output_.str(), "\"{\"master_key\":\"*****************\"}\"\n");
}

// -- Message truncation -------------------------------------------------------

TEST_F(LoggerFixture, truncates_oversized_message)
{
    initLogging(false);
    Logger logger("Trunc");
    // 12 * 1024 = 12288 max chars; create a message larger than that
    static constexpr std::size_t kMAX = 12 * 1024;
    std::string const bigMessage(13000, 'x');
    logger.info() << bigMessage;

    // Expected: (kMAX - 3) 'x' chars + "..." + newline
    std::string expected(kMAX - 3, 'x');
    expected += "...\n";
    EXPECT_EQ(output_.str(), expected);
}

TEST_F(LoggerFixture, truncates_oversized_message_json)
{
    initLogging(true);
    Logger logger("Trunc");
    static constexpr std::size_t kMAX = 12 * 1024;
    std::string const bigMessage(13000, 'x');
    logger.info() << bigMessage << log::param("key", std::string("value"));

    // and the values object is preserved in full.
    std::string expected = "\"";
    expected += std::string(kMAX - 3, 'x');
    expected += "...\", \"values\": {\"key\":\"value\"}\n";
    EXPECT_EQ(output_.str(), expected);
}
