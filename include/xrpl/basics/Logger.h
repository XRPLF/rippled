#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/StructuredLogging.h>

#include <fmt/format.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

// We forward declare spdlog types to avoid including the spdlog headers in this header file.
namespace spdlog {

class logger;     // NOLINT(readability-identifier-naming)
class formatter;  // NOLINT(readability-identifier-naming)

namespace sinks {
class sink;  // NOLINT(readability-identifier-naming)
}  // namespace sinks

}  // namespace spdlog

struct BenchmarkLoggingInitializer;
class LoggerFixture;
struct LogServiceInitTests;
struct LogFileRotationTests;

namespace xrpl {

/**
 * @brief Skips evaluation of expensive argument lists if the given logger is disabled for the
 * required severity level.
 *
 * Note: Currently this introduces potential shadowing (unlikely).
 */
#ifndef COVERAGE_ENABLED
#define LOG(x)                                 \
    if (auto xrpl_pump__ = x; not xrpl_pump__) \
    {                                          \
    }                                          \
    else                                       \
        xrpl_pump__
#else
#define LOG(x) x
#endif

/**
 * @brief Custom severity levels for @ref util::Logger.
 */
enum class Severity {
    TRC,
    DBG,
    NFO,
    WRN,
    ERR,
    FTL,
};

/**
 * @brief The default log format pattern for non-JSON mode.
 *
 * Matches the legacy Logs::format output:
 *   2024-Jan-15 12:34:56.789123 UTC General:NFO hello world
 *
 * '%K' is a custom flag that outputs the three-letter severity code
 * (TRC, DBG, NFO, WRN, ERR, FTL).
 */
inline constexpr char const* kDEFAULT_LOG_FORMAT = "%Y-%b-%d %H:%M:%S.%f UTC %n:%K %v";

struct LoggingConfiguration
{
    std::string format{kDEFAULT_LOG_FORMAT};
    bool enableConsole;
    std::optional<std::string> directory;
    bool isAsync;
    Severity defaultSeverity;
    bool jsonMode{false};
};

/**
 * @brief A simple thread-safe logger for the channel specified
 * in the constructor.
 *
 * This is cheap to copy and move. Designed to be used as a member variable or
 * otherwise. See @ref LogService::init() for setup of the logging core and
 * severity levels for each channel.
 */
class Logger
{
    std::shared_ptr<spdlog::logger> logger_;

    friend class LogService;  // to expose the Pump interface

    /**
     * @brief Helper that pumps data into a log record via `operator<<`.
     */
    class Pump final
    {
        std::shared_ptr<spdlog::logger> logger_;
        Severity const severity_;
        std::source_location const sourceLocation_;
        std::string stream_;
        bool const enabled_;
        bool const jsonMode_;
        std::string parameters_;  // accumulated JSON parameter fragments

    public:
        ~Pump();

        Pump(
            std::shared_ptr<spdlog::logger> logger,
            Severity sev,
            std::source_location const& loc,
            bool jsonMode);

        Pump(Pump&&) = delete;
        Pump(Pump const&) = delete;
        Pump&
        operator=(Pump const&) = delete;
        Pump&
        operator=(Pump&&) = delete;

        /**
         * @brief Appends data that has an @c xrpl::to_string overload.
         *
         * This covers all XRP Ledger domain types such as @c Number,
         * @c Currency, @c AccountID, @c XRPAmount, @c IOUAmount, @c Asset, etc.
         *
         * @tparam T Type of data to pump (must satisfy detail::HasToString)
         * @param data The data to pump
         * @return Reference to itself for chaining
         */
        template <typename T>
            requires(detail::HasToString<T>)
        [[maybe_unused]] Pump&
        operator<<(T&& data)
        {
            if (enabled_)
                stream_ += to_string(data);
            return *this;
        }

        /**
         * @brief Appends any fmt-formattable data into the output string.
         *
         * Fallback for types that do not have an @c xrpl::to_string overload
         * but can be formatted by @c fmt::format (e.g. arithmetic types,
         * @c std::string, @c std::string_view).
         *
         * @tparam T Type of data to pump
         * @param data The data to pump
         * @return Reference to itself for chaining
         */
        template <typename T>
            requires(!detail::HasToString<T>)
        [[maybe_unused]] Pump&
        operator<<(T&& data)
        {
            if (enabled_)
                fmt::format_to(std::back_inserter(stream_), "{}", std::forward<T>(data));
            return *this;
        }

        /**
         * @brief Captures a structured log parameter.
         *
         * The parameter value is always appended to the output string.
         * In JSON mode, the parameter is also accumulated into the
         * parameters string for the "values" object emitted in the
         * destructor.
         *
         * @tparam T Type of the parameter value
         * @param p The parameter to capture
         * @return Reference to itself for chaining
         */
        template <typename T>
        [[maybe_unused]] Pump&
        operator<<(xrpl::log::Parameter<T>&& p)
        {
            if (!enabled_)
                return *this;

            // Append the raw string representation to the output stream
            if constexpr (detail::HasToString<T>)
            {
                stream_ += to_string(p.value());
            }
            else
            {
                fmt::format_to(std::back_inserter(stream_), "{}", p.value());
            }

            if (jsonMode_)
            {
                // Also build up the parameters for the "values" object
                if (!parameters_.empty())
                    parameters_ += ',';
                fmt::format_to(std::back_inserter(parameters_), "\"{}\":", p.name());
                detail::appendJsonValue(parameters_, p.value());
            }

            return *this;
        }

        /**
         * @return true if logger is enabled; false otherwise
         */
        operator bool() const
        {
            return enabled_;
        }
    };

public:
    /**
     * @brief Construct a new Logger object that produces loglines for the
     * specified channel.
     *
     * See @ref LogService::init() for general setup and configuration of
     * severity levels per channel.
     *
     * @param channel The channel this logger will report into.
     */
    Logger(std::string_view const channel);

    Logger(Logger const&) = default;
    ~Logger();

    Logger(Logger&&) = default;
    Logger&
    operator=(Logger const&) = default;

    Logger&
    operator=(Logger&&) = default;

    /**
     * @brief Interface for logging at Severity::TRC severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    trace(std::source_location const& loc = std::source_location::current()) const;

    /**
     * @brief Interface for logging at Severity::DBG severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    debug(std::source_location const& loc = std::source_location::current()) const;

    /**
     * @brief Interface for logging at Severity::NFO severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    info(std::source_location const& loc = std::source_location::current()) const;

    /**
     * @brief Interface for logging at Severity::WRN severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    warn(std::source_location const& loc = std::source_location::current()) const;

    /**
     * @brief Interface for logging at Severity::ERR severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    error(std::source_location const& loc = std::source_location::current()) const;

    /**
     * @brief Interface for logging at Severity::FTL severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] Pump
    fatal(std::source_location const& loc = std::source_location::current()) const;

private:
    Logger(std::shared_ptr<spdlog::logger> logger);
};

/**
 * @brief Base state management class for the logging service.
 *
 * This class manages the global state and core functionality for the logging system,
 * including initialization, sink management, and logger registration.
 */
class LogServiceState
{
protected:
    friend struct ::LogServiceInitTests;
    friend class ::LoggerFixture;
    friend struct ::LogFileRotationTests;
    friend class Logger;

    /**
     * @brief Initialize the logging core with specified parameters.
     *
     * @param isAsync Whether logging should be asynchronous
     * @param defaultSeverity The default severity level for new loggers
     * @param sinks Vector of spdlog sinks to use for output
     */
    static void
    init(
        bool isAsync,
        Severity defaultSeverity,
        std::vector<std::shared_ptr<spdlog::sinks::sink>> const& sinks,
        bool jsonMode = false);

    /**
     * @brief Whether the LogService is initialized or not
     *
     * @return true if the LogService is initialized
     */
    [[nodiscard]] static bool
    initialized();

    /**
     * @brief Whether the LogService has any sink. If there is no sink, logger will not log messages
     * anywhere.
     *
     * @return true if the LogService has at least one sink
     */
    [[nodiscard]] static bool
    hasSinks();

    /**
     * @brief Reset the logging service to uninitialized state.
     */
    static void
    reset();

    /**
     * @brief Register a new logger for the specified channel.
     *
     * Creates and registers a new spdlog logger instance for the given channel
     * with the specified or default severity level.
     *
     * @param channel The name of the logging channel
     * @param severity Optional severity level override; uses default if not specified
     * @return Shared pointer to the registered spdlog logger
     */
    static std::shared_ptr<spdlog::logger>
    registerLogger(std::string_view channel, std::optional<Severity> severity = std::nullopt);

    /**
     * @brief Replace the current sinks with a new set of sinks.
     *
     * @param sinks Vector of new spdlog sinks to replace the current ones
     */
    static void
    replaceSinks(std::vector<std::shared_ptr<spdlog::sinks::sink>> const& sinks);

    /**
     * @brief Creates a pattern formatter with custom flags (e.g. '%K' for
     * severity) and UTC timestamps.
     *
     * @param pattern The spdlog pattern string
     * @return A unique_ptr to the configured pattern_formatter
     */
    static std::unique_ptr<spdlog::formatter>
    makeFormatter(std::string const& pattern);

protected:
    static bool isAsync_;              // NOLINT(readability-identifier-naming)
    static Severity defaultSeverity_;  // NOLINT(readability-identifier-naming)
    static std::vector<std::shared_ptr<spdlog::sinks::sink>>
        sinks_;                                 // NOLINT(readability-identifier-naming)
    static bool initialized_;                   // NOLINT(readability-identifier-naming)
    static bool jsonMode_;                      // NOLINT(readability-identifier-naming)
    static std::optional<std::string> logDir_;  // NOLINT(readability-identifier-naming)
    static std::string format_;                 // NOLINT(readability-identifier-naming)
};

/**
 * @brief A global logging service.
 *
 * Used to initialize and setup the logging core as well as a globally available
 * entrypoint for logging into the `General` channel as well as raising alerts.
 */
class LogService : public LogServiceState
{
public:
    LogService() = delete;

    /**
     * @brief Global log core initialization from a @ref LoggingConfiguration
     *
     * @param config The configuration to use
     * @return Void on success, error message on failure
     */
    [[nodiscard]] static Expected<void, std::string>
    init(LoggingConfiguration const& config);

    /**
     * @brief Shutdown spdlog to guarantee output is not lost
     */
    static void
    shutdown();

    /**
     * @brief Close and reopen the log file.
     *
     * This assists in interoperating with external log management tools
     * such as logrotate(8). If no file logging is configured, this is a no-op.
     *
     * @return A human-readable status message
     */
    [[nodiscard]] static std::string
    rotate();

    /**
     * @brief Globally accessible General logger at Severity::TRC severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    trace(std::source_location const& loc = std::source_location::current());

    /**
     * @brief Globally accessible General logger at Severity::DBG severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    debug(std::source_location const& loc = std::source_location::current());

    /**
     * @brief Globally accessible General logger at Severity::NFO severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    info(std::source_location const& loc = std::source_location::current());

    /**
     * @brief Globally accessible General logger at Severity::WRN severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    warn(std::source_location const& loc = std::source_location::current());

    /**
     * @brief Globally accessible General logger at Severity::ERR severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    error(std::source_location const& loc = std::source_location::current());

    /**
     * @brief Globally accessible General logger at Severity::FTL severity
     *
     * @param loc The source location of the log message
     * @return The pump to use for logging
     */
    [[nodiscard]] static Logger::Pump
    fatal(std::source_location const& loc = std::source_location::current());

private:
    /**
     * @brief Parses the sinks from a @ref LoggingConfiguration
     *
     * @param config The configuration to parse sinks from
     * @return A vector of sinks on success, error message on failure
     */
    [[nodiscard]] static Expected<std::vector<std::shared_ptr<spdlog::sinks::sink>>, std::string>
    getSinks(LoggingConfiguration const& config);

    struct FileLoggingParams
    {
        std::string logDir;
    };

    friend struct ::BenchmarkLoggingInitializer;
    friend class ::LoggerFixture;

    [[nodiscard]]
    static std::shared_ptr<spdlog::sinks::sink>
    createFileSink(FileLoggingParams const& params, std::string const& format);
};

};  // namespace xrpl
