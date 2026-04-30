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

    // Regex fragment matching the UTC timestamp produced by spdlog
    static constexpr char kTS_RE[] = R"(\d{4}-[A-Z][a-z]{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} UTC)";

    void
    initLogging(bool jsonMode, Severity severity = Severity::TRC)
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

        // Replace the (empty) sinks with our ostream sink using the
        // effective format chosen by LogService (text or JSON).
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output_);
        sink->set_level(spdlog::level::trace);
        sink->set_formatter(LogServiceState::makeFormatter(LogServiceState::format()));
        LogServiceState::replaceSinks({sink});
    }

    /// Escape special regex characters in a literal string.
    static std::string
    escapeRegex(std::string_view s)
    {
        std::string result;
        for (char c : s)
        {
            if (std::string_view(R"(\^$.|?*+()[]{}-)").find(c) != std::string_view::npos)
                result += '\\';
            result += c;
        }
        return result;
    }

    /// Assert that the captured output matches the expected text-mode line.
    /// Format: <timestamp> channel:sev message\n
    void
    expectText(std::string_view channel, std::string_view sev, std::string_view message)
    {
        auto const& line = output_.str();
        auto const re = fmt::format("{} {}:{} {}\n", kTS_RE, channel, sev, escapeRegex(message));
        EXPECT_TRUE(std::regex_match(line, std::regex(re))) << "got: " << line;
    }

    /// Assert that the captured output matches the expected JSON-mode line.
    /// Format: {"timestamp":"...","channel":"...","severity":"...",
    ///          "message": <msgPart> }\n
    void
    expectJson(std::string_view channel, std::string_view sev, std::string_view msgPart)
    {
        auto const& line = output_.str();
        auto const re = fmt::format(
            R"(\{{"timestamp":"{}","channel":"{}","severity":"{}")"
            R"(, "message": {} \}}\n)",
            kTS_RE,
            channel,
            sev,
            escapeRegex(msgPart));
        EXPECT_TRUE(std::regex_match(line, std::regex(re))) << "got: " << line;
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
    expectText("TestChannel", "NFO", "hello world");
}

TEST_F(LoggerFixture, plain_text_multiple_values)
{
    initLogging(false);
    Logger logger("TestChannel2");
    logger.info() << "count=" << 42 << " active=" << true;
    expectText("TestChannel2", "NFO", "count=42 active=true");
}

TEST_F(LoggerFixture, plain_text_with_parameter)
{
    initLogging(false);
    Logger logger("TestChannel3");
    logger.info() << "tx " << log::param("hash", "ABC");
    // In plain text mode, parameter value is just streamed
    expectText("TestChannel3", "NFO", "tx ABC");
}

// -- JSON mode ---------------------------------------------------------------

TEST_F(LoggerFixture, json_mode_simple_message)
{
    initLogging(true);
    Logger logger("JsonChannel");
    logger.info() << "hello world";
    expectJson("JsonChannel", "NFO", "\"hello world\"");
}

TEST_F(LoggerFixture, json_mode_with_parameters)
{
    initLogging(true);
    Logger logger("JsonChannel2");
    logger.info() << "processing " << log::param("tx_hash", std::string("ABC123"))
                  << " amount=" << log::param("amount", 42);
    expectJson(
        "JsonChannel2",
        "NFO",
        "\"processing ABC123 amount=42\", "
        "\"values\": {\"tx_hash\":\"ABC123\",\"amount\":42}");
}

TEST_F(LoggerFixture, json_mode_no_parameters)
{
    initLogging(true);
    Logger logger("JsonChannel3");
    logger.info() << "simple message";
    expectJson("JsonChannel3", "NFO", "\"simple message\"");
}

TEST_F(LoggerFixture, json_mode_bool_parameter)
{
    initLogging(true);
    Logger logger("JsonChannel4");
    logger.info() << "status " << log::param("active", true);
    expectJson("JsonChannel4", "NFO", "\"status true\", \"values\": {\"active\":true}");
}

// -- Severity filtering ------------------------------------------------------

TEST_F(LoggerFixture, severity_filtering)
{
    initLogging(false, Severity::WRN);

    Logger logger("FilterChannel");
    logger.info() << "should not appear";
    EXPECT_TRUE(output_.str().empty());

    logger.warn() << "should appear";
    expectText("FilterChannel", "WRN", "should appear");
}

// -- xrpl::to_string integration --------------------------------------------

TEST_F(LoggerFixture, text_mode_xrp_amount)
{
    initLogging(false);
    Logger logger("AmountChannel");
    logger.info() << "balance: " << XRPAmount{1000};
    expectText("AmountChannel", "NFO", "balance: 1000");
}

TEST_F(LoggerFixture, json_mode_xrp_amount)
{
    initLogging(true);
    Logger logger("AmountChannel");
    logger.info() << "balance " << XRPAmount{500};
    expectJson("AmountChannel", "NFO", "\"balance 500\"");
}

TEST_F(LoggerFixture, json_mode_xrp_amount_parameter)
{
    initLogging(true);
    Logger logger("AmountChannel");
    logger.info() << "tx" << log::param("fee", XRPAmount{10});
    expectJson("AmountChannel", "NFO", "\"tx10\", \"values\": {\"fee\":\"10\"}");
}

TEST_F(LoggerFixture, text_mode_number)
{
    initLogging(false);
    Logger logger("NumberChannel");
    logger.info() << "result: " << Number{42};
    expectText("NumberChannel", "NFO", "result: 42");
}

TEST_F(LoggerFixture, json_mode_number)
{
    initLogging(true);
    Logger logger("NumberChannel");
    logger.info() << "value " << Number{25, -3};
    expectJson("NumberChannel", "NFO", "\"value 0.025\"");
}

TEST_F(LoggerFixture, json_mode_number_parameter)
{
    initLogging(true);
    Logger logger("NumberChannel");
    logger.info() << "calc" << log::param("rate", Number{100});
    expectJson("NumberChannel", "NFO", "\"calc100\", \"values\": {\"rate\":\"100\"}");
}

// -- Severity codes -----------------------------------------------------------

TEST_F(LoggerFixture, severity_codes_in_default_format)
{
    initLogging(false);
    Logger logger("Test");

    logger.trace() << "t";
    logger.debug() << "d";
    logger.info() << "i";
    logger.warn() << "w";
    logger.error() << "e";
    logger.fatal() << "f";

    // Each line has the full default format; build a regex for all six.
    auto const line = [&](std::string_view sev, std::string_view msg) {
        return fmt::format("{} Test:{} {}\n", kTS_RE, sev, msg);
    };
    std::string re;
    re += line("TRC", "t");
    re += line("DBG", "d");
    re += line("NFO", "i");
    re += line("WRN", "w");
    re += line("ERR", "e");
    re += line("FTL", "f");
    EXPECT_TRUE(std::regex_match(output_.str(), std::regex(re))) << "got: " << output_.str();
}

// -- Pattern builder function -------------------------------------------------

TEST_F(LoggerFixture, build_json_pattern_from_scratch)
{
    auto pattern = buildJsonPattern(
        "",
        log::param("channel", std::string_view("%n")),
        log::param("level", std::string_view("%l")));
    EXPECT_EQ(pattern, R"({"channel":"%n","level":"%l", "message": %v })");
}

TEST_F(LoggerFixture, build_json_pattern_extends_existing)
{
    auto const base = buildJsonPattern("", log::param("channel", std::string_view("%n")));
    auto const extended = buildJsonPattern(base, log::param("trace_id", std::string("abc123")));
    EXPECT_EQ(extended, R"({"channel":"%n","trace_id":"abc123", "message": %v })");
}

// -- Logger context inheritance -----------------------------------------------

TEST_F(LoggerFixture, child_logger_inherits_context_params)
{
    initLogging(true);
    Logger parent("Parent");
    Logger child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "hello";
    expectJson("Child", "NFO", "\"hello\", \"values\": {\"peer_id\":\"abc\"}");
}

TEST_F(LoggerFixture, child_logger_merges_context_and_message_params)
{
    initLogging(true);
    Logger parent("Parent");
    Logger child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "event" << log::param("count", 42);
    expectJson("Child", "NFO", "\"event42\", \"values\": {\"peer_id\":\"abc\",\"count\":42}");
}

TEST_F(LoggerFixture, grandchild_logger_accumulates_context)
{
    initLogging(true);
    Logger root("Root");
    Logger child(root, "Child", log::param("trace_id", std::string("t1")));
    Logger grandchild(child, "Grandchild", log::param("span_id", std::string("s1")));
    grandchild.info() << "deep";
    expectJson(
        "Grandchild", "NFO", "\"deep\", \"values\": {\"trace_id\":\"t1\",\"span_id\":\"s1\"}");
}

TEST_F(LoggerFixture, context_params_in_text_mode)
{
    initLogging(false);
    Logger parent("Parent");
    Logger child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "hello";
    // In text mode, context params are shown as [key=val] prefix
    expectText("Child", "NFO", "[peer_id=abc] hello");
}

TEST_F(LoggerFixture, context_params_text_mode_multiple)
{
    initLogging(false);
    Logger root("Root");
    Logger child(root, "Child", log::param("trace_id", std::string("t1")));
    Logger grandchild(child, "GC", log::param("span_id", std::string("s1")));
    grandchild.info() << "deep";
    expectText("GC", "NFO", "[trace_id=t1 span_id=s1] deep");
}

// -- Secret scrubbing ---------------------------------------------------------

TEST_F(LoggerFixture, scrubs_seed)
{
    initLogging(false);
    Logger logger("Scrub");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // 31 chars in the seed value → 31 asterisks
    expectText("Scrub", "NFO", R"({"seed":"*******************************"})");
}

TEST_F(LoggerFixture, scrubs_master_key)
{
    initLogging(false);
    Logger logger("Scrub2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    expectText("Scrub2", "NFO", R"({"master_key":"*****************"})");
}

TEST_F(LoggerFixture, scrubs_passphrase)
{
    initLogging(false);
    Logger logger("Scrub3");
    logger.info() << R"({"passphrase":"my_secret_pass"})";
    expectText("Scrub3", "NFO", R"({"passphrase":"**************"})");
}

TEST_F(LoggerFixture, scrubs_seed_json_mode)
{
    initLogging(true);
    Logger logger("ScrubJson");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // In JSON mode the message is wrapped in quotes, but scrubbing still works
    expectJson("ScrubJson", "NFO", "\"{\"seed\":\"*******************************\"}\"");
}

TEST_F(LoggerFixture, scrubs_master_key_json_mode)
{
    initLogging(true);
    Logger logger("ScrubJson2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    expectJson("ScrubJson2", "NFO", "\"{\"master_key\":\"*****************\"}\"");
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

    // The message body is truncated; full line includes timestamp prefix.
    std::string truncatedMsg(kMAX - 3, 'x');
    truncatedMsg += "...";
    expectText("Trunc", "NFO", truncatedMsg);
}

TEST_F(LoggerFixture, truncates_oversized_message_json)
{
    initLogging(true);
    Logger logger("Trunc");
    static constexpr std::size_t kMAX = 12 * 1024;
    std::string const bigMessage(13000, 'x');
    logger.info() << bigMessage << log::param("key", std::string("value"));

    // Message body is truncated; values object is preserved in full.
    std::string msgPart = "\"";
    msgPart += std::string(kMAX - 3, 'x');
    msgPart += "...\", \"values\": {\"key\":\"value\"}";
    expectJson("Trunc", "NFO", msgPart);
}
