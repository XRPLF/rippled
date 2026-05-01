#include <xrpl/basics/Logger.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/StructuredLogging.h>
#include <xrpl/protocol/XRPAmount.h>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/async_logger.h>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace xrpl;

class LoggerFixture : public ::testing::Test
{
protected:
    std::ostringstream output_;

    // Regex fragment matching the UTC timestamp produced by spdlog
    static constexpr char kTS_RE[] = R"(\d{4}-[A-Z][a-z]{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} UTC)";

    void
    initLogging(bool jsonMode, Severity severity = Severity::TRC, bool isAsync = false)
    {
        LoggingConfiguration const config{
            .enableConsole = false,
            .directory = std::nullopt,
            .isAsync = isAsync,
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
        for (char const c : s)
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

    /// Access the current sinks vector (friend access).
    static std::vector<std::shared_ptr<spdlog::sinks::sink>> const&
    sinks()
    {
        return LogServiceState::sinks_;
    }

    /// Register a logger with an optional severity override (friend access).
    static std::shared_ptr<spdlog::logger>
    registerLogger(std::string_view channel, std::optional<Severity> severity = std::nullopt)
    {
        return LogServiceState::registerLogger(channel, severity);
    }

    /// Whether the LogService has any sinks (friend access).
    static bool
    hasSinks()
    {
        return LogServiceState::hasSinks();
    }

    /// Reset the LogService (friend access).
    static void
    resetService()
    {
        LogServiceState::reset();
    }

    /// Install two sinks: one with NonCriticalFormatter, one critical-only.
    /// Returns {non-critical output, critical output}.
    static std::pair<std::ostringstream*, std::ostringstream*>
    installSplitSinks(std::ostringstream& ncOut, std::ostringstream& critOut)
    {
        auto ncSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(ncOut);
        ncSink->set_level(spdlog::level::trace);
        ncSink->set_formatter(
            LogServiceState::makeNonCriticalFormatter(
                LogServiceState::makeFormatter(LogServiceState::format())));

        auto critSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(critOut);
        critSink->set_level(spdlog::level::critical);
        critSink->set_formatter(LogServiceState::makeFormatter(LogServiceState::format()));

        LogServiceState::replaceSinks({ncSink, critSink});
        return {&ncOut, &critOut};
    }

    /// Install a single sink wrapped with NonCriticalFormatter.
    static void
    installNonCriticalSink(std::ostringstream& out)
    {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
        sink->set_level(spdlog::level::trace);
        sink->set_formatter(
            LogServiceState::makeNonCriticalFormatter(
                LogServiceState::makeFormatter(LogServiceState::format())));
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
    Logger const logger("TestChannel");
    logger.info() << "hello world";
    expectText("TestChannel", "NFO", "hello world");
}

TEST_F(LoggerFixture, plain_text_multiple_values)
{
    initLogging(false);
    Logger const logger("TestChannel2");
    logger.info() << "count=" << 42 << " active=" << true;
    expectText("TestChannel2", "NFO", "count=42 active=true");
}

TEST_F(LoggerFixture, plain_text_with_parameter)
{
    initLogging(false);
    Logger const logger("TestChannel3");
    logger.info() << "tx " << log::param("hash", "ABC");
    // In plain text mode, parameter value is just streamed
    expectText("TestChannel3", "NFO", "tx ABC");
}

// -- JSON mode ---------------------------------------------------------------

TEST_F(LoggerFixture, json_mode_simple_message)
{
    initLogging(true);
    Logger const logger("JsonChannel");
    logger.info() << "hello world";
    expectJson("JsonChannel", "NFO", "\"hello world\"");
}

TEST_F(LoggerFixture, json_mode_with_parameters)
{
    initLogging(true);
    Logger const logger("JsonChannel2");
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
    Logger const logger("JsonChannel3");
    logger.info() << "simple message";
    expectJson("JsonChannel3", "NFO", "\"simple message\"");
}

TEST_F(LoggerFixture, json_mode_bool_parameter)
{
    initLogging(true);
    Logger const logger("JsonChannel4");
    logger.info() << "status " << log::param("active", true);
    expectJson("JsonChannel4", "NFO", "\"status true\", \"values\": {\"active\":true}");
}

// -- Severity filtering ------------------------------------------------------

TEST_F(LoggerFixture, severity_filtering)
{
    initLogging(false, Severity::WRN);

    Logger const logger("FilterChannel");
    logger.info() << "should not appear";
    EXPECT_TRUE(output_.str().empty());

    logger.warn() << "should appear";
    expectText("FilterChannel", "WRN", "should appear");
}

// -- xrpl::to_string integration --------------------------------------------

TEST_F(LoggerFixture, text_mode_xrp_amount)
{
    initLogging(false);
    Logger const logger("AmountChannel");
    logger.info() << "balance: " << XRPAmount{1000};
    expectText("AmountChannel", "NFO", "balance: 1000");
}

TEST_F(LoggerFixture, json_mode_xrp_amount)
{
    initLogging(true);
    Logger const logger("AmountChannel");
    logger.info() << "balance " << XRPAmount{500};
    expectJson("AmountChannel", "NFO", "\"balance 500\"");
}

TEST_F(LoggerFixture, json_mode_xrp_amount_parameter)
{
    initLogging(true);
    Logger const logger("AmountChannel");
    logger.info() << "tx" << log::param("fee", XRPAmount{10});
    expectJson("AmountChannel", "NFO", "\"tx10\", \"values\": {\"fee\":\"10\"}");
}

TEST_F(LoggerFixture, text_mode_number)
{
    initLogging(false);
    Logger const logger("NumberChannel");
    logger.info() << "result: " << Number{42};
    expectText("NumberChannel", "NFO", "result: 42");
}

TEST_F(LoggerFixture, json_mode_number)
{
    initLogging(true);
    Logger const logger("NumberChannel");
    logger.info() << "value " << Number{25, -3};
    expectJson("NumberChannel", "NFO", "\"value 0.025\"");
}

TEST_F(LoggerFixture, json_mode_number_parameter)
{
    initLogging(true);
    Logger const logger("NumberChannel");
    logger.info() << "calc" << log::param("rate", Number{100});
    expectJson("NumberChannel", "NFO", "\"calc100\", \"values\": {\"rate\":\"100\"}");
}

// -- Severity codes -----------------------------------------------------------

TEST_F(LoggerFixture, severity_codes_in_default_format)
{
    initLogging(false);
    Logger const logger("Test");

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

// -- NonCriticalFormatter ------------------------------------------------------

TEST_F(LoggerFixture, non_critical_formatter_passes_non_critical)
{
    initLogging(false);

    std::ostringstream ncOutput;
    installNonCriticalSink(ncOutput);

    Logger const logger("NCTest");
    logger.info() << "hello";

    // Non-critical message should appear
    EXPECT_FALSE(ncOutput.str().empty()) << "Non-critical message was suppressed";
    EXPECT_NE(ncOutput.str().find("hello"), std::string::npos);
}

TEST_F(LoggerFixture, non_critical_formatter_suppresses_critical)
{
    initLogging(false);

    std::ostringstream ncOutput;
    installNonCriticalSink(ncOutput);

    Logger const logger("NCTest2");
    logger.fatal() << "critical message";

    // Critical (fatal) message should be suppressed
    EXPECT_TRUE(ncOutput.str().empty())
        << "Critical message was not suppressed: " << ncOutput.str();
}

TEST_F(LoggerFixture, non_critical_formatter_splits_output)
{
    initLogging(false);

    std::ostringstream stdoutOutput;
    std::ostringstream stderrOutput;
    installSplitSinks(stdoutOutput, stderrOutput);

    Logger const logger("SplitTest");
    logger.info() << "normal message";
    logger.fatal() << "fatal message";

    // stdout sink: should have the info message but NOT the fatal message
    EXPECT_NE(stdoutOutput.str().find("normal message"), std::string::npos)
        << "stdout: " << stdoutOutput.str();
    EXPECT_EQ(stdoutOutput.str().find("fatal message"), std::string::npos)
        << "stdout should not have fatal: " << stdoutOutput.str();

    // stderr sink: should have the fatal message but NOT the info message
    EXPECT_NE(stderrOutput.str().find("fatal message"), std::string::npos)
        << "stderr: " << stderrOutput.str();
    EXPECT_EQ(stderrOutput.str().find("normal message"), std::string::npos)
        << "stderr should not have info: " << stderrOutput.str();
}

TEST_F(LoggerFixture, non_critical_formatter_all_non_critical_levels)
{
    initLogging(false);

    std::ostringstream ncOutput;
    installNonCriticalSink(ncOutput);

    Logger const logger("LevelTest");
    logger.trace() << "t";
    logger.debug() << "d";
    logger.info() << "i";
    logger.warn() << "w";
    logger.error() << "e";

    // All five non-critical levels should appear
    auto const& out = ncOutput.str();
    EXPECT_NE(out.find("TRC"), std::string::npos) << out;
    EXPECT_NE(out.find("DBG"), std::string::npos) << out;
    EXPECT_NE(out.find("NFO"), std::string::npos) << out;
    EXPECT_NE(out.find("WRN"), std::string::npos) << out;
    EXPECT_NE(out.find("ERR"), std::string::npos) << out;

    // Now log a fatal and verify it does NOT appear
    logger.fatal() << "f";
    EXPECT_EQ(out.find("FTL"), std::string::npos)
        << "FTL should not appear in NonCriticalFormatter output: " << out;
}

TEST_F(LoggerFixture, console_enabled_creates_stdout_and_stderr_sinks)
{
    // With enableConsole = true, init should create both a stdout sink
    // (wrapped in NonCriticalFormatter) and a stderr sink (critical only).
    LoggingConfiguration const config{
        .enableConsole = true,
        .directory = std::nullopt,
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    ASSERT_TRUE(result);

    // enableConsole=true → stdout sink + stderr sink = 2 sinks
    EXPECT_EQ(sinks().size(), 2u);

    // First sink should be a stdout_color_sink_mt
    EXPECT_NE(dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sinks()[0].get()), nullptr)
        << "First sink should be stdout_color_sink_mt";

    // Second sink should be a stderr_color_sink_mt
    EXPECT_NE(dynamic_cast<spdlog::sinks::stderr_color_sink_mt*>(sinks()[1].get()), nullptr)
        << "Second sink should be stderr_color_sink_mt";
}

TEST_F(LoggerFixture, console_disabled_creates_only_stderr_sink)
{
    // With enableConsole = false, init should only create the stderr sink.
    LoggingConfiguration const config{
        .enableConsole = false,
        .directory = std::nullopt,
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    ASSERT_TRUE(result);

    // enableConsole=false → stderr sink only = 1 sink
    EXPECT_EQ(sinks().size(), 1u);

    EXPECT_NE(dynamic_cast<spdlog::sinks::stderr_color_sink_mt*>(sinks()[0].get()), nullptr)
        << "Only sink should be stderr_color_sink_mt";
}

TEST_F(LoggerFixture, file_sink_created_when_directory_configured)
{
    // Use a unique temp directory for the log file
    auto const tmpDir =
        std::filesystem::temp_directory_path() / ("logger_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tmpDir);

    LoggingConfiguration const config{
        .enableConsole = false,
        .directory = tmpDir.string(),
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    ASSERT_TRUE(result);

    // stderr sink + file sink = 2 sinks
    ASSERT_EQ(sinks().size(), 2u);
    EXPECT_NE(dynamic_cast<spdlog::sinks::basic_file_sink_mt*>(sinks()[1].get()), nullptr)
        << "Second sink should be basic_file_sink_mt";

    // The log file should exist
    EXPECT_TRUE(std::filesystem::exists(tmpDir / "xrpld.log"));

    // Clean up
    std::filesystem::remove_all(tmpDir);
}

TEST_F(LoggerFixture, file_sink_writes_log_entries)
{
    auto const tmpDir = std::filesystem::temp_directory_path() /
        ("logger_test_write_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tmpDir);

    LoggingConfiguration const config{
        .enableConsole = false,
        .directory = tmpDir.string(),
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    ASSERT_TRUE(result);

    Logger const logger("FileTest");
    logger.info() << "file sink test message";

    // Flush so the message is written
    spdlog::default_logger()->flush();

    // Read the file and verify the message was written
    auto const logPath = tmpDir / "xrpld.log";
    std::ifstream ifs(logPath);
    std::string const contents(
        (std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(contents.find("file sink test message"), std::string::npos)
        << "Log file contents: " << contents;
    EXPECT_NE(contents.find("FileTest:NFO"), std::string::npos)
        << "Log file contents: " << contents;

    // Clean up
    std::filesystem::remove_all(tmpDir);
}

TEST_F(LoggerFixture, init_fails_for_invalid_directory)
{
    // Use a path where directory creation should fail —
    // a deeply nested path under a non-existent root that can't be created.
    // On macOS/Linux, /proc/fake/... or a path with a null byte should fail.
    LoggingConfiguration const config{
        .enableConsole = false,
        .directory = std::string("/dev/null/impossible_log_dir"),
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    EXPECT_FALSE(result) << "init should fail for an invalid directory";
}

// -- Async logging ------------------------------------------------------------

TEST_F(LoggerFixture, async_mode_creates_async_logger)
{
    initLogging(/*jsonMode=*/false, Severity::TRC, /*isAsync=*/true);

    Logger const logger("AsyncTest");
    auto const spdlogger = spdlog::get("AsyncTest");
    ASSERT_NE(spdlogger, nullptr);
    EXPECT_NE(dynamic_cast<spdlog::async_logger*>(spdlogger.get()), nullptr)
        << "Logger should be async_logger when isAsync=true";
}

TEST_F(LoggerFixture, async_mode_delivers_messages)
{
    initLogging(/*jsonMode=*/false, Severity::TRC, /*isAsync=*/true);

    Logger const logger("AsyncMsg");
    logger.info() << "async hello";

    // In async mode, messages are queued to a thread pool.
    // shutdown() blocks until all queued messages are processed.
    LogService::shutdown();

    EXPECT_NE(output_.str().find("async hello"), std::string::npos)
        << "Async message not delivered: " << output_.str();
    EXPECT_NE(output_.str().find("AsyncMsg:NFO"), std::string::npos)
        << "Missing channel/severity: " << output_.str();
}

TEST_F(LoggerFixture, sync_mode_creates_sync_logger)
{
    initLogging(false);

    Logger const logger("SyncTest");
    auto const spdlogger = spdlog::get("SyncTest");
    ASSERT_NE(spdlogger, nullptr);
    EXPECT_EQ(dynamic_cast<spdlog::async_logger*>(spdlogger.get()), nullptr)
        << "Logger should be a sync logger when isAsync=false";
}

// -- Double initialisation guard ----------------------------------------------

TEST_F(LoggerFixture, double_init_throws)
{
    initLogging(false);

    // Second init should throw because LogServiceState is already initialized
    LoggingConfiguration const config{};
    EXPECT_THROW(LogService::init(config), std::logic_error);
}

TEST_F(LoggerFixture, reset_before_init_throws)
{
    // reset() without prior init should throw
    EXPECT_THROW(resetService(), std::logic_error);
}

TEST_F(LoggerFixture, has_sinks_after_init)
{
    EXPECT_FALSE(hasSinks()) << "No sinks before init";

    initLogging(false);
    EXPECT_TRUE(hasSinks()) << "Should have sinks after init";
}

TEST_F(LoggerFixture, has_no_sinks_after_reset)
{
    initLogging(false);
    EXPECT_TRUE(hasSinks());

    resetService();
    EXPECT_FALSE(hasSinks()) << "Sinks should be cleared after reset";
}

TEST_F(LoggerFixture, register_logger_before_init_throws)
{
    // registerLogger without prior init should throw
    EXPECT_THROW(registerLogger("Uninitialized"), std::logic_error);
}

// -- registerLogger re-registration -------------------------------------------

TEST_F(LoggerFixture, register_existing_logger_returns_same_instance)
{
    initLogging(false);

    // First registration creates the logger
    auto const first = registerLogger("DuplicateChannel");
    ASSERT_NE(first, nullptr);

    // Second registration should return the same logger instance
    auto const second = registerLogger("DuplicateChannel");
    EXPECT_EQ(first.get(), second.get());
}

TEST_F(LoggerFixture, register_existing_logger_updates_severity)
{
    initLogging(false);

    // Create logger at default severity (TRC → spdlog::level::trace)
    auto const logger = registerLogger("SevChannel");
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::trace);

    // Re-register with a severity override
    auto const same = registerLogger("SevChannel", Severity::ERR);
    EXPECT_EQ(same.get(), logger.get()) << "Should return the same instance";
    EXPECT_EQ(logger->level(), spdlog::level::err) << "Level should be updated to ERR";
}

TEST_F(LoggerFixture, register_existing_logger_without_severity_keeps_level)
{
    initLogging(false);

    // Create logger with an explicit severity
    auto const logger = registerLogger("KeepChannel", Severity::WRN);
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::warn);

    // Re-register without severity — level should remain unchanged
    auto const same = registerLogger("KeepChannel");
    EXPECT_EQ(same.get(), logger.get());
    EXPECT_EQ(logger->level(), spdlog::level::warn) << "Level should remain WRN";
}

// -- LogService static convenience methods ------------------------------------

TEST_F(LoggerFixture, log_service_trace)
{
    initLogging(false);
    LogService::trace() << "trace msg";
    expectText("General", "TRC", "trace msg");
}

TEST_F(LoggerFixture, log_service_debug)
{
    initLogging(false);
    LogService::debug() << "debug msg";
    expectText("General", "DBG", "debug msg");
}

TEST_F(LoggerFixture, log_service_info)
{
    initLogging(false);
    // init already logs "Default log level = TRC" on the General channel,
    // so clear and re-test.
    output_.str({});
    LogService::info() << "info msg";
    expectText("General", "NFO", "info msg");
}

TEST_F(LoggerFixture, log_service_warn)
{
    initLogging(false);
    output_.str({});
    LogService::warn() << "warn msg";
    expectText("General", "WRN", "warn msg");
}

TEST_F(LoggerFixture, log_service_error)
{
    initLogging(false);
    output_.str({});
    LogService::error() << "error msg";
    expectText("General", "ERR", "error msg");
}

TEST_F(LoggerFixture, log_service_fatal)
{
    initLogging(false);
    output_.str({});
    LogService::fatal() << "fatal msg";
    expectText("General", "FTL", "fatal msg");
}

// -- LogService::rotate -------------------------------------------------------

TEST_F(LoggerFixture, rotate_returns_message_when_no_file_logging)
{
    initLogging(false);
    auto const msg = LogService::rotate();
    EXPECT_EQ(msg, "Log file rotation is not possible because file logging is not configured.");
}

TEST_F(LoggerFixture, rotate_replaces_file_sink)
{
    auto const tmpDir = std::filesystem::temp_directory_path() /
        ("logger_test_rotate_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tmpDir);

    LoggingConfiguration const config{
        .enableConsole = false,
        .directory = tmpDir.string(),
        .isAsync = false,
        .defaultSeverity = Severity::TRC,
        .jsonMode = false,
    };
    auto const result = LogService::init(config);
    ASSERT_TRUE(result);

    // Log something before rotation
    Logger const logger("RotateTest");
    logger.info() << "before rotation";

    auto const msg = LogService::rotate();
    EXPECT_EQ(msg, "The log file was closed and reopened.");

    // The file sink should still be a basic_file_sink_mt after rotation
    bool hasFileSink = false;
    for (auto const& s : sinks())
    {
        if (dynamic_cast<spdlog::sinks::basic_file_sink_mt*>(s.get()) != nullptr)
        {
            hasFileSink = true;
            break;
        }
    }
    EXPECT_TRUE(hasFileSink) << "File sink should still exist after rotation";

    // Log something after rotation — it should go to the new file
    logger.info() << "after rotation";
    spdlog::get("RotateTest")->flush();

    std::ifstream ifs(tmpDir / "xrpld.log");
    std::string const contents(
        (std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("after rotation"), std::string::npos)
        << "Post-rotation message missing from log file: " << contents;

    std::filesystem::remove_all(tmpDir);
}

// -- LogService::shutdown -----------------------------------------------------

TEST_F(LoggerFixture, shutdown_sync_mode_is_noop)
{
    initLogging(false);
    // shutdown() should not throw or crash in sync mode
    LogService::shutdown();
    // We can still log after shutdown in sync mode
    Logger const logger("ShutdownSync");
    logger.info() << "still works";
    EXPECT_NE(output_.str().find("still works"), std::string::npos);
}

TEST_F(LoggerFixture, shutdown_async_mode_flushes)
{
    initLogging(/*jsonMode=*/false, Severity::TRC, /*isAsync=*/true);

    Logger const logger("ShutdownAsync");
    logger.info() << "before shutdown";

    LogService::shutdown();

    EXPECT_NE(output_.str().find("before shutdown"), std::string::npos)
        << "Message should be flushed by shutdown: " << output_.str();
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
    Logger const parent("Parent");
    Logger const child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "hello";
    expectJson("Child", "NFO", "\"hello\", \"values\": {\"peer_id\":\"abc\"}");
}

TEST_F(LoggerFixture, child_logger_merges_context_and_message_params)
{
    initLogging(true);
    Logger const parent("Parent");
    Logger const child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "event" << log::param("count", 42);
    expectJson("Child", "NFO", "\"event42\", \"values\": {\"peer_id\":\"abc\",\"count\":42}");
}

TEST_F(LoggerFixture, grandchild_logger_accumulates_context)
{
    initLogging(true);
    Logger const root("Root");
    Logger const child(root, "Child", log::param("trace_id", std::string("t1")));
    Logger const grandchild(child, "Grandchild", log::param("span_id", std::string("s1")));
    grandchild.info() << "deep";
    expectJson(
        "Grandchild", "NFO", "\"deep\", \"values\": {\"trace_id\":\"t1\",\"span_id\":\"s1\"}");
}

TEST_F(LoggerFixture, context_params_in_text_mode)
{
    initLogging(false);
    Logger const parent("Parent");
    Logger const child(parent, "Child", log::param("peer_id", std::string("abc")));
    child.info() << "hello";
    // In text mode, context params are shown as [key=val] prefix
    expectText("Child", "NFO", "[peer_id=abc] hello");
}

TEST_F(LoggerFixture, context_params_text_mode_multiple)
{
    initLogging(false);
    Logger const root("Root");
    Logger const child(root, "Child", log::param("trace_id", std::string("t1")));
    Logger const grandchild(child, "GC", log::param("span_id", std::string("s1")));
    grandchild.info() << "deep";
    expectText("GC", "NFO", "[trace_id=t1 span_id=s1] deep");
}

// -- Secret scrubbing ---------------------------------------------------------

TEST_F(LoggerFixture, scrubs_seed)
{
    initLogging(false);
    Logger const logger("Scrub");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // 31 chars in the seed value → 31 asterisks
    expectText("Scrub", "NFO", R"({"seed":"*******************************"})");
}

TEST_F(LoggerFixture, scrubs_master_key)
{
    initLogging(false);
    Logger const logger("Scrub2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    expectText("Scrub2", "NFO", R"({"master_key":"*****************"})");
}

TEST_F(LoggerFixture, scrubs_passphrase)
{
    initLogging(false);
    Logger const logger("Scrub3");
    logger.info() << R"({"passphrase":"my_secret_pass"})";
    expectText("Scrub3", "NFO", R"({"passphrase":"**************"})");
}

TEST_F(LoggerFixture, scrubs_seed_json_mode)
{
    initLogging(true);
    Logger const logger("ScrubJson");
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH6HsouMaM2"})";
    // In JSON mode the message is wrapped in quotes, but scrubbing still works
    expectJson("ScrubJson", "NFO", "\"{\"seed\":\"*******************************\"}\"");
}

TEST_F(LoggerFixture, scrubs_master_key_json_mode)
{
    initLogging(true);
    Logger const logger("ScrubJson2");
    logger.info() << R"({"master_key":"SOME_SECRET_VALUE"})";
    expectJson("ScrubJson2", "NFO", "\"{\"master_key\":\"*****************\"}\"");
}

TEST_F(LoggerFixture, scrubs_seed_without_closing_quote)
{
    initLogging(false);
    Logger const logger("ScrubNoClose");
    // The value has an opening quote after "seed": but no closing quote.
    // The scrubber should treat everything to end-of-string as the secret.
    logger.info() << R"({"seed":"sEdTM1uX8pu2do5XvTnutH)";
    // 22 chars in "sEdTM1uX8pu2do5XvTnutH" → 22 asterisks
    expectText("ScrubNoClose", "NFO", R"({"seed":"**********************)");
}

// -- Message truncation -------------------------------------------------------

TEST_F(LoggerFixture, truncates_oversized_message)
{
    initLogging(false);
    Logger const logger("Trunc");
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
    Logger const logger("Trunc");
    static constexpr std::size_t kMAX = 12 * 1024;
    std::string const bigMessage(13000, 'x');
    logger.info() << bigMessage << log::param("key", std::string("value"));

    // Message body is truncated; values object is preserved in full.
    std::string msgPart = "\"";
    msgPart += std::string(kMAX - 3, 'x');
    msgPart += "...\", \"values\": {\"key\":\"value\"}";
    expectJson("Trunc", "NFO", msgPart);
}
