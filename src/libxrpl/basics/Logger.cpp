#include <xrpl/basics/Logger.h>

#include <xrpl/basics/Expected.h>

#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace xrpl {

bool LogServiceState::isAsync_{true};
Severity LogServiceState::defaultSeverity_{Severity::NFO};
std::vector<spdlog::sink_ptr> LogServiceState::sinks_{};
bool LogServiceState::initialized_{false};
bool LogServiceState::jsonMode_{false};
std::optional<std::string> LogServiceState::logDir_{};
std::string LogServiceState::format_{};

namespace {

spdlog::level::level_enum
toSpdlogLevel(Severity sev)
{
    switch (sev)
    {
        case Severity::TRC:
            return spdlog::level::trace;
        case Severity::DBG:
            return spdlog::level::debug;
        case Severity::NFO:
            return spdlog::level::info;
        case Severity::WRN:
            return spdlog::level::warn;
        case Severity::ERR:
            return spdlog::level::err;
        case Severity::FTL:
            return spdlog::level::critical;
    }
    return spdlog::level::info;
}

std::string_view
toString(Severity sev)
{
    static constexpr std::array<std::string_view, 6> kLABELS = {
        "TRC",
        "DBG",
        "NFO",
        "WRN",
        "ERR",
        "FTL",
    };

    return kLABELS.at(static_cast<int>(sev));
}

}  // namespace

/**
 * @brief Custom formatter that filters out critical messages
 *
 * This formatter only processes and formats messages with severity level less than critical.
 * Critical messages will be handled separately.
 */
class NonCriticalFormatter : public spdlog::formatter
{
public:
    NonCriticalFormatter(std::unique_ptr<spdlog::formatter> wrappedFormatter)
        : wrapped_formatter_(std::move(wrappedFormatter))
    {
    }

    void
    format(spdlog::details::log_msg const& msg, spdlog::memory_buf_t& dest) override
    {
        // Only format messages with severity less than critical
        if (msg.level != spdlog::level::critical)
        {
            wrapped_formatter_->format(msg, dest);
        }
    }

    [[nodiscard]] std::unique_ptr<formatter>
    clone() const override
    {
        return std::make_unique<NonCriticalFormatter>(wrapped_formatter_->clone());
    }

private:
    std::unique_ptr<spdlog::formatter> wrapped_formatter_;
};

/**
 * @brief Custom spdlog flag formatter that outputs the three-letter severity
 * codes used by the legacy logging system: TRC, DBG, NFO, WRN, ERR, FTL.
 *
 * Registered as the '%K' custom flag in pattern formatters created by
 * @ref createPatternFormatter.
 */
class SeverityFlagFormatter final : public spdlog::custom_flag_formatter
{
public:
    void
    format(
        spdlog::details::log_msg const& msg,
        std::tm const& /*tm_time*/,
        spdlog::memory_buf_t& dest) override
    {
        static constexpr std::array<std::string_view, 7> kLABELS = {
            "TRC",
            "DBG",
            "NFO",
            "WRN",
            "ERR",
            "FTL",
            "OFF",
        };

        auto const idx = static_cast<std::size_t>(msg.level);
        auto const label = idx < kLABELS.size() ? kLABELS[idx] : "???";
        dest.append(label.data(), label.data() + label.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter>
    clone() const override
    {
        return std::make_unique<SeverityFlagFormatter>();
    }
};

/**
 * @brief Creates a spdlog pattern_formatter with our custom '%K' severity flag
 * and UTC timestamps.
 *
 * @param pattern The spdlog pattern string (should use '%K' for severity)
 * @return A unique_ptr to the configured pattern_formatter
 */
static std::unique_ptr<spdlog::pattern_formatter>
createPatternFormatter(std::string const& pattern)
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>(
        pattern, spdlog::pattern_time_type::utc, spdlog::details::os::default_eol);
    formatter->add_flag<SeverityFlagFormatter>('K');
    formatter->set_pattern(pattern);
    return formatter;
}

/**
 * @brief Truncates a log message to the maximum allowed length, appending
 * an ellipsis if truncation occurs.
 *
 * Matches the legacy Logs::format behaviour with maximumMessageCharacters = 12 * 1024.
 *
 * @param message The message to potentially truncate (modified in-place)
 */
static void
truncateMessage(fmt::memory_buffer& message)
{
    static constexpr std::size_t kMAX_MESSAGE_CHARS = 12 * 1024;
    if (message.size() > kMAX_MESSAGE_CHARS)
    {
        message.resize(kMAX_MESSAGE_CHARS - 3);
        static constexpr char kELLIPSIS[] = "...";
        message.append(kELLIPSIS, kELLIPSIS + 3);
    }
}

/**
 * @brief Scrubs sensitive fields from a log message by replacing their values
 * with asterisks.
 *
 * Ported from the legacy Logs::format scrubber. Looks for JSON-like tokens
 * such as "seed", "secret", "master_key", etc., finds the value enclosed in
 * double quotes immediately after the token, and replaces the value characters
 * with '*'.
 *
 * @param output The log message to scrub (modified in-place)
 */
static void
scrubSecrets(fmt::memory_buffer& output)
{
    // Fast path: if there's no double-quote anywhere in the message,
    // none of the JSON-like tokens can possibly match.
    std::string_view const view{output.data(), output.size()};
    if (view.find('"') == std::string_view::npos)
        return;

    // We need string operations (find/replace) so convert temporarily.
    // This is only reached for messages that contain at least one '"'.
    std::string tmp{view};

    auto scrubber = [&tmp](char const* token) {
        auto first = tmp.find(token);

        if (first != std::string::npos)
        {
            first = tmp.find('\"', first + std::strlen(token));

            if (first != std::string::npos)
            {
                auto last = tmp.find('\"', ++first);

                if (last == std::string::npos)
                    last = tmp.size();

                tmp.replace(first, last - first, last - first, '*');
            }
        }
    };

    scrubber("\"seed\"");
    scrubber("\"seed_hex\"");
    scrubber("\"secret\"");
    scrubber("\"master_key\"");
    scrubber("\"master_seed\"");
    scrubber("\"master_seed_hex\"");
    scrubber("\"passphrase\"");

    // Copy the scrubbed result back into the buffer
    output.clear();
    output.append(tmp.data(), tmp.data() + tmp.size());
}

/**
 * @brief Initializes console logging.
 *
 * @param logToConsole A boolean indicating whether to log to console.
 * @param format A string representing the log format.
 * @return Vector of sinks for console logging.
 */
static std::vector<spdlog::sink_ptr>
createConsoleSinks(bool logToConsole, std::string const& format)
{
    std::vector<spdlog::sink_ptr> sinks;

    if (logToConsole)
    {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::trace);
        consoleSink->set_formatter(
            std::make_unique<NonCriticalFormatter>(createPatternFormatter(format)));
        sinks.push_back(std::move(consoleSink));
    }

    // Always add stderr sink for fatal logs
    auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    stderrSink->set_level(spdlog::level::critical);
    stderrSink->set_formatter(createPatternFormatter(format));
    sinks.push_back(std::move(stderrSink));

    return sinks;
}

/**
 * @brief Initializes file logging.
 *
 * @param config The configuration object containing log settings.
 * @param dirPath The directory path where log files will be stored.
 * @return File sink for logging.
 */
spdlog::sink_ptr
LogService::createFileSink(FileLoggingParams const& params, std::string const& format)
{
    std::filesystem::path const dirPath(params.logDir);
    auto fileSink = [&]() -> std::shared_ptr<spdlog::sinks::sink> {
        auto const logPath = (dirPath / "xrpld.log").string();
        return std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, /*truncate=*/false);
    }();

    fileSink->set_level(spdlog::level::trace);
    fileSink->set_formatter(createPatternFormatter(format));

    return fileSink;
}

void
LogServiceState::init(
    bool isAsync,
    Severity defaultSeverity,
    std::vector<spdlog::sink_ptr> const& sinks,
    bool jsonMode)
{
    if (initialized_)
    {
        throw std::logic_error("LogServiceState is already initialized");
    }

    isAsync_ = isAsync;
    defaultSeverity_ = defaultSeverity;
    sinks_ = sinks;
    jsonMode_ = jsonMode;
    initialized_ = true;

    spdlog::apply_all([](std::shared_ptr<spdlog::logger> logger) {
        logger->set_level(toSpdlogLevel(defaultSeverity_));
    });

    if (isAsync)
    {
        static constexpr size_t kQUEUE_SIZE = 8192;
        static constexpr size_t kTHREAD_COUNT = 1;
        spdlog::init_thread_pool(kQUEUE_SIZE, kTHREAD_COUNT);
    }
}

bool
LogServiceState::initialized()
{
    return initialized_;
}

bool
LogServiceState::hasSinks()
{
    return not sinks_.empty();
}

void
LogServiceState::reset()
{
    if (not initialized())
    {
        throw std::logic_error("LogService is not initialized");
    }
    isAsync_ = true;
    defaultSeverity_ = Severity::NFO;
    sinks_.clear();
    jsonMode_ = false;
    logDir_.reset();
    format_.clear();
    initialized_ = false;
}

std::shared_ptr<spdlog::logger>
LogServiceState::registerLogger(std::string_view channel, std::optional<Severity> severity)
{
    if (not initialized_)
    {
        throw std::logic_error("LogService is not initialized");
    }

    std::string const channelStr{channel};

    std::shared_ptr<spdlog::logger> existingLogger = spdlog::get(channelStr);
    if (existingLogger != nullptr)
    {
        if (severity.has_value())
            existingLogger->set_level(toSpdlogLevel(*severity));
        return existingLogger;
    }

    std::shared_ptr<spdlog::logger> logger;
    if (isAsync_)
    {
        logger = std::make_shared<spdlog::async_logger>(
            channelStr,
            sinks_.begin(),
            sinks_.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
    }
    else
    {
        logger = std::make_shared<spdlog::logger>(channelStr, sinks_.begin(), sinks_.end());
    }

    logger->set_level(toSpdlogLevel(severity.value_or(defaultSeverity_)));
    logger->flush_on(spdlog::level::err);

    spdlog::register_logger(logger);

    return logger;
}

Expected<std::vector<spdlog::sink_ptr>, std::string>
LogService::getSinks(LoggingConfiguration const& config, std::string const& format)
{
    std::vector<spdlog::sink_ptr> allSinks = createConsoleSinks(config.enableConsole, format);

    if (config.directory.has_value())
    {
        std::filesystem::path const dirPath{config.directory.value()};
        if (not std::filesystem::exists(dirPath))
        {
            if (std::error_code error; not std::filesystem::create_directories(dirPath, error))
            {
                return Unexpected{fmt::format(
                    "Couldn't create logs directory '{}': {}", dirPath.string(), error.message())};
            }
        }

        FileLoggingParams const params{
            .logDir = config.directory.value(),
        };
        allSinks.push_back(createFileSink(params, format));
    }
    return allSinks;
}

Expected<void, std::string>
LogService::init(LoggingConfiguration const& config)
{
    // Format is fully determined by the logging mode.
    format_ = config.jsonMode ? defaultJsonLogFormat() : std::string(kDEFAULT_LOG_FORMAT);

    auto const sinksMaybe = getSinks(config, format_);
    if (!sinksMaybe.has_value())
    {
        return Unexpected{sinksMaybe.error()};
    }

    logDir_ = config.directory;

    LogServiceState::init(
        config.isAsync, config.defaultSeverity, std::move(sinksMaybe).value(), config.jsonMode);

    // Register and set the "General" channel as the default logger.
    // All other channels are created dynamically at runtime via Logger(channel).
    spdlog::set_default_logger(registerLogger("General"));

    LOG(LogService::info()) << "Default log level = " << toString(defaultSeverity_);
    return {};
}

void
LogService::shutdown()
{
    if (initialized_ && isAsync_)
    {
        // We run in async mode in production, so we need to make sure all logs are flushed before
        // shutting down
        spdlog::shutdown();
    }
}

std::string
LogService::rotate()
{
    if (!logDir_.has_value())
    {
        return "Log file rotation is not possible because file logging is not configured.";
    }

    FileLoggingParams const params{.logDir = logDir_.value()};

    auto newFileSink = createFileSink(params, format_);

    // Replace any existing file sink with the new one
    for (auto& sink : sinks_)
    {
        if (dynamic_cast<spdlog::sinks::basic_file_sink_mt*>(sink.get()) != nullptr)
        {
            sink = newFileSink;
            break;
        }
    }

    // Update all registered loggers with the new sinks
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> logger) { logger->sinks() = sinks_; });

    return "The log file was closed and reopened.";
}

Logger::Pump
LogService::trace(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).trace(loc);
}

Logger::Pump
LogService::debug(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).debug(loc);
}

Logger::Pump
LogService::info(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).info(loc);
}

Logger::Pump
LogService::warn(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).warn(loc);
}

Logger::Pump
LogService::error(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).error(loc);
}

Logger::Pump
LogService::fatal(std::source_location const& loc)
{
    return Logger(spdlog::default_logger()).fatal(loc);
}

void
LogServiceState::replaceSinks(std::vector<std::shared_ptr<spdlog::sinks::sink>> const& sinks)
{
    sinks_ = sinks;
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> logger) { logger->sinks() = sinks_; });
}

std::unique_ptr<spdlog::formatter>
LogServiceState::makeFormatter(std::string const& pattern)
{
    return createPatternFormatter(pattern);
}

Logger::Logger(std::string_view const channel)
    : logger_(LogServiceState::registerLogger(channel)), jsonMode_(LogServiceState::jsonMode_)
{
}

Logger::~Logger()
{
    // One reference is held by logger_ and the other by spdlog registry
    static constexpr size_t LAST_LOGGER_REF_COUNT = 2;

    if (logger_ == nullptr)
    {
        return;
    }

    if (logger_.use_count() == LAST_LOGGER_REF_COUNT)
    {
        spdlog::drop(logger_->name());
    }
}

Logger::Pump::Pump(
    spdlog::logger* logger,
    Severity sev,
    std::source_location const& loc,
    bool jsonMode,
    std::string_view contextParams)
    : logger_(logger)
    , severity_(sev)
    , sourceLocation_(loc)
    , enabled_(logger_ != nullptr && logger_->should_log(toSpdlogLevel(sev)))
    , jsonMode_(jsonMode)
    , contextParams_(contextParams)
{
}

Logger::Pump::~Pump()
{
    if (enabled_)
    {
        spdlog::source_loc const sourceLocation{
            sourceLocation_.file_name(), static_cast<int>(sourceLocation_.line()), nullptr};

        // Apply legacy safeguards on the raw message BEFORE JSON wrapping.
        // This lets scrubSecrets short-circuit (no '"' in typical messages).
        truncateMessage(stream_);
        scrubSecrets(stream_);

        if (jsonMode_)
        {
            // Wrap the scrubbed message: "<message>", plus optional values object
            fmt::memory_buffer wrapped;
            wrapped.push_back('"');
            wrapped.append(stream_.data(), stream_.data() + stream_.size());
            wrapped.push_back('"');

            bool const hasContext = !contextParams_.empty();
            bool const hasMessage = !messageParams_.empty();
            if (hasContext || hasMessage)
            {
                static constexpr char valuesOpen[] = ", \"values\": {";
                wrapped.append(valuesOpen, valuesOpen + sizeof(valuesOpen) - 1);
                wrapped.append(
                    contextParams_.data(), contextParams_.data() + contextParams_.size());
                if (hasContext && hasMessage)
                    wrapped.push_back(',');
                wrapped.append(
                    messageParams_.data(), messageParams_.data() + messageParams_.size());
                wrapped.push_back('}');
            }

            logger_->log(
                sourceLocation,
                toSpdlogLevel(severity_),
                std::string_view{wrapped.data(), wrapped.size()});
        }
        else
        {
            if (!contextParams_.empty())
            {
                // Prepend [key=val ...] context to the message
                fmt::memory_buffer buf;
                buf.push_back('[');
                buf.append(contextParams_.data(), contextParams_.data() + contextParams_.size());
                static constexpr char close[] = "] ";
                buf.append(close, close + 2);
                buf.append(stream_.data(), stream_.data() + stream_.size());
                logger_->log(
                    sourceLocation,
                    toSpdlogLevel(severity_),
                    std::string_view{buf.data(), buf.size()});
            }
            else
            {
                logger_->log(
                    sourceLocation,
                    toSpdlogLevel(severity_),
                    std::string_view{stream_.data(), stream_.size()});
            }
        }
    }
}

Logger::Pump
Logger::trace(std::source_location const& loc) const
{
    return {logger_.get(), Severity::TRC, loc, LogServiceState::jsonMode_, contextParams_};
}
Logger::Pump
Logger::debug(std::source_location const& loc) const
{
    return {logger_.get(), Severity::DBG, loc, LogServiceState::jsonMode_, contextParams_};
}
Logger::Pump
Logger::info(std::source_location const& loc) const
{
    return {logger_.get(), Severity::NFO, loc, LogServiceState::jsonMode_, contextParams_};
}
Logger::Pump
Logger::warn(std::source_location const& loc) const
{
    return {logger_.get(), Severity::WRN, loc, LogServiceState::jsonMode_, contextParams_};
}
Logger::Pump
Logger::error(std::source_location const& loc) const
{
    return {logger_.get(), Severity::ERR, loc, LogServiceState::jsonMode_, contextParams_};
}
Logger::Pump
Logger::fatal(std::source_location const& loc) const
{
    return {logger_.get(), Severity::FTL, loc, LogServiceState::jsonMode_, contextParams_};
}

Logger::Logger(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)), jsonMode_(LogServiceState::jsonMode_)
{
}

}  // namespace xrpl
