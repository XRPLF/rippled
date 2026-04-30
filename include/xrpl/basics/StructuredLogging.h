#pragma once

#include <fmt/format.h>

#include <concepts>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace xrpl {

namespace detail {

/**
 * @brief Concept that detects whether @c to_string(t) is a valid expression
 * returning something convertible to @c std::string.
 *
 * Because this concept lives inside @c namespace @c xrpl, unqualified lookup
 * will find every @c xrpl::to_string overload (e.g. for @c Number,
 * @c Currency, @c AccountID, @c XRPAmount, @c IOUAmount, @c Asset, etc.).
 * ADL further extends the search to any namespace associated with @p T.
 */
template <typename T>
concept HasToString = requires(std::remove_cvref_t<T> const& t) {
    { to_string(t) } -> std::convertible_to<std::string>;
};

/**
 * @brief Append a value formatted as a JSON fragment to @p dest.
 *
 * - @c bool \u2192 unquoted @c true / @c false
 * - integral (non-bool) \u2192 unquoted number
 * - floating-point \u2192 unquoted number
 * - types with @c to_string \u2192 quoted string via @c to_string
 * - everything else \u2192 quoted string obtained via @c fmt::format
 *
 * @tparam T The value type.
 * @param dest The string to append the JSON fragment to.
 * @param value The value to format.
 */
template <typename T>
void
appendJsonValue(std::string& dest, T const& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        dest += value ? "true" : "false";
    }
    else if constexpr (std::is_integral_v<T>)
    {
        dest += std::to_string(value);
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        dest += std::to_string(value);
    }
    else if constexpr (HasToString<T>)
    {
        fmt::format_to(std::back_inserter(dest), "\"{}\"", to_string(value));
    }
    else
    {
        fmt::format_to(std::back_inserter(dest), "\"{}\"", value);
    }
}

}  // namespace detail

/**
 * @brief Builds a JSON-structured spdlog format pattern string.
 *
 * This class produces a pattern string suitable for passing to
 * @c spdlog::pattern_formatter. Each field maps a JSON key to an spdlog
 * pattern flag (e.g. @c %t for thread ID, @c %n for channel name).
 *
 * The pattern ends with @c %%v so that the log message (a partial JSON
 * fragment) is spliced in at the end.
 *
 * Composability is lightweight: construct from an existing pattern string
 * (e.g. obtained from another logger's pattern) and append more fields.
 * No map copying — just string concatenation.
 *
 * Common spdlog pattern flags:
 *  - @c %%v  — the log message text
 *  - @c %%t  — thread ID
 *  - @c %%n  — logger/channel name
 *  - @c %%l  — log level (e.g. "info", "warning")
 *  - @c %%Y-%%m-%%d %%H:%%M:%%S.%%e — date/time with milliseconds
 *  - @c %%s  — source file name
 *  - @c %%#  — source line number
 *  - @c %%P  — process ID
 *
 * Example usage:
 * @code
 *     // Build a fresh pattern
 *     JsonLoggingPatternBuilder builder;
 *     builder.add("timestamp", "%Y-%m-%d %H:%M:%S.%e");
 *     builder.add("channel", "%n");
 *     builder.add("level", "%l");
 *     std::string pattern = builder.build();
 *     // Produces: {"timestamp":"%Y-%m-%d %H:%M:%S.%e","channel":"%n","level":"%l" %v }
 *
 *     // Extend an existing pattern from another logger
 *     JsonLoggingPatternBuilder extended(pattern);
 *     extended.add("source", "%s:%#");
 *     std::string newPattern = extended.build();
 *     // Produces: {"timestamp":"%Y-%m-%d %H:%M:%S.%e","channel":"%n","level":"%l","source":"%s:%#"
 * %v }
 * @endcode
 */
class JsonLoggingPatternBuilder
{
    // Accumulated "key":"value" fragments, comma-separated.
    // e.g. "\"timestamp\":\"%Y-%m-%d\",\"channel\":\"%n\""
    std::string fields_;

    static constexpr std::string_view kSUFFIX = ", \"message\": %v }";

    /**
     * @brief Extract the fields portion from an existing pattern string.
     *
     * Strips the leading '{' and trailing ' %%v }' suffix, leaving just
     * the comma-separated field fragments.
     */
    static std::string
    extractFields(std::string_view pattern)
    {
        // Strip leading '{'
        if (!pattern.empty() && pattern.front() == '{')
            pattern.remove_prefix(1);

        // Strip trailing ' %v }'
        if (auto pos = pattern.rfind(kSUFFIX); pos != std::string_view::npos)
            pattern = pattern.substr(0, pos);

        return std::string(pattern);
    }

public:
    /** @brief Construct an empty builder. */
    JsonLoggingPatternBuilder() = default;

    /**
     * @brief Construct from an existing pattern string.
     *
     * Extracts the fields from the pattern so that new fields can be
     * appended. This is a lightweight string copy — no map involved.
     *
     * @param existingPattern A pattern previously produced by build().
     */
    explicit JsonLoggingPatternBuilder(std::string_view existingPattern)
        : fields_(extractFields(existingPattern))
    {
    }

    /**
     * @brief Add a field to the JSON pattern.
     *
     * The value is an spdlog pattern flag or any literal text that spdlog
     * will expand at log time.
     *
     * @param key The JSON field name (e.g. "timestamp", "thread").
     * @param value The spdlog pattern string (e.g. "%%Y-%%m-%%d", "%%t").
     * @return Reference to this builder for chaining.
     */
    JsonLoggingPatternBuilder&
    add(std::string_view key, std::string_view value)
    {
        if (!fields_.empty())
            fields_ += ',';
        fmt::format_to(std::back_inserter(fields_), "\"{}\":\"{}\"", key, value);
        return *this;
    }

    /**
     * @brief Add a typed field to the JSON pattern.
     *
     * Uses @ref detail::appendJsonValue to format the value as a JSON
     * fragment (unquoted for numbers/bools, quoted for everything else).
     *
     * @tparam T The value type (bool, integral, floating-point, or streamable).
     * @param key The JSON field name.
     * @param value The value to embed.
     * @return Reference to this builder for chaining.
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    JsonLoggingPatternBuilder&
    add(std::string_view key, T const& value)
    {
        if (!fields_.empty())
            fields_ += ',';
        fmt::format_to(std::back_inserter(fields_), "\"{}\":", key);
        detail::appendJsonValue(fields_, value);
        return *this;
    }

    /**
     * @brief Build the final spdlog-compatible pattern string.
     *
     * Produces a compact JSON object where each value contains the raw
     * spdlog pattern flags, terminated with %%v for the message body:
     * @code
     *   {"level":"%l","channel":"%n" %v }
     * @endcode
     *
     * @return The pattern string to pass to spdlog::pattern_formatter.
     */
    [[nodiscard]] std::string
    build() const
    {
        return "{" + fields_ + std::string(kSUFFIX);
    }
};

namespace log {

/**
 * @brief Holds a named log parameter with its value.
 *
 * Constructed via the @ref param helper function. Designed to be passed
 * into Logger::Pump (the integration with Pump will be added later).
 *
 * @tparam T The value type.
 */
template <typename T>
class Parameter
{
    std::string name_;
    T value_;

public:
    Parameter(std::string_view name, T const& value) : name_(name), value_(value)
    {
    }

    /** @brief Get the parameter name. */
    [[nodiscard]] std::string_view
    name() const
    {
        return name_;
    }

    /** @brief Get the parameter value. */
    [[nodiscard]] T const&
    value() const
    {
        return value_;
    }
};

/**
 * @brief Create a named log parameter.
 *
 * @tparam T The value type (deduced).
 * @param name The parameter name (JSON key).
 * @param value The parameter value.
 * @return A Parameter holding the name and value.
 */
template <typename T>
[[nodiscard]] Parameter<std::decay_t<T>>
param(std::string_view name, T&& value)
{
    return Parameter<std::decay_t<T>>{name, value};
}

}  // namespace log

}  // namespace xrpl
