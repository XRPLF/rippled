#pragma once

#include <fmt/format.h>

#include <concepts>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
 * @brief Escape a string for safe embedding in a JSON value.
 *
 * Escapes backslash, double-quote, and control characters (U+0000-U+001F)
 * according to RFC 8259 section 7.
 */
inline void
appendEscapedJsonString(std::string& dest, std::string_view sv)
{
    dest.reserve(dest.size() + sv.size() + 2);
    dest += '"';
    for (char const c : sv)
    {
        switch (c)
        {
            case '"':
                dest += "\\\"";
                break;
            case '\\':
                dest += "\\\\";
                break;
            case '\b':
                dest += "\\b";
                break;
            case '\f':
                dest += "\\f";
                break;
            case '\n':
                dest += "\\n";
                break;
            case '\r':
                dest += "\\r";
                break;
            case '\t':
                dest += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    fmt::format_to(
                        std::back_inserter(dest), "\\u{:04x}", static_cast<unsigned int>(c));
                }
                else
                {
                    dest += c;
                }
                break;
        }
    }
    dest += '"';
}

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
        appendEscapedJsonString(dest, to_string(value));
    }
    else
    {
        appendEscapedJsonString(dest, fmt::format("{}", value));
    }
}

/**
 * @brief Append a @c "key":value JSON field to @p dest.
 *
 * Inserts a comma separator when @p dest already contains fields.
 * The value is formatted by @ref appendJsonValue.
 */
template <typename T>
void
appendJsonField(std::string& dest, std::string_view key, T const& value)
{
    if (!dest.empty())
        dest += ',';
    appendEscapedJsonString(dest, key);
    dest += ':';
    appendJsonValue(dest, value);
}

/**
 * @brief Append a @c key=value plain-text field to @p dest.
 *
 * Inserts a space separator when @p dest already contains fields.
 */
template <typename T>
void
appendTextField(std::string& dest, std::string_view key, T const& value)
{
    if (!dest.empty())
        dest += ' ';
    if constexpr (HasToString<T>)
    {
        fmt::format_to(std::back_inserter(dest), "{}={}", key, to_string(value));
    }
    else
    {
        fmt::format_to(std::back_inserter(dest), "{}={}", key, value);
    }
}

/** @brief Internal builder used by @ref buildJsonPattern.
 *
 *  Not part of the public API — use @ref buildJsonPattern instead.
 */
class JsonLoggingPatternBuilder
{
    std::string fields_;

    static constexpr std::string_view kMESSAGE_FIELD = "\"message\": %v";

    static std::string
    extractFields(std::string_view pattern)
    {
        if (!pattern.empty() && pattern.front() == '{')
            pattern.remove_prefix(1);
        // Strip the trailing message field: `"message": %v }` or `"message": %v }`
        if (auto pos = pattern.rfind(kMESSAGE_FIELD); pos != std::string_view::npos)
        {
            // Also strip the leading ", " separator if present
            auto end = pos;
            if (end >= 2 && pattern.substr(end - 2, 2) == ", ")
                end -= 2;
            pattern = pattern.substr(0, end);
        }
        return std::string(pattern);
    }

public:
    JsonLoggingPatternBuilder() = default;

    explicit JsonLoggingPatternBuilder(std::string_view existingPattern)
        : fields_(extractFields(existingPattern))
    {
    }

    /** @brief Add a field. Delegates to @ref appendJsonField. */
    template <typename T>
    JsonLoggingPatternBuilder&
    add(std::string_view key, T const& value)
    {
        appendJsonField(fields_, key, value);
        return *this;
    }

    [[nodiscard]] std::string
    build() const
    {
        std::string result = "{";
        result += fields_;
        if (!fields_.empty())
            result += ", ";
        result += kMESSAGE_FIELD;
        result += " }";
        return result;
    }
};

}  // namespace detail

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
    Parameter(std::string_view name, T value) : name_(name), value_(std::move(value))
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
    value() const&
    {
        return value_;
    }

    /** @brief Get the parameter value. */
    [[nodiscard]] T&
    value() &
    {
        return value_;
    }

    /** @brief Get the parameter value. */
    [[nodiscard]] T&&
    value() &&
    {
        return std::move(value_);
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

/**
 * @brief Build or extend a JSON log pattern from @ref log::Parameter objects.
 *
 * Each parameter's name becomes a JSON key and its value becomes the
 * corresponding JSON value in the spdlog pattern string.  String-like
 * values are treated as spdlog pattern flags (e.g. @c "%%n", @c "%%l");
 * numeric and boolean values are embedded as literals.
 *
 * @code
 *     // Build from scratch (empty existing pattern)
 *     auto pattern = buildJsonPattern("",
 *         log::param("channel", std::string_view("%n")),
 *         log::param("level",   std::string_view("%l")));
 *
 *     // Extend an existing pattern with extra fields
 *     auto extended = buildJsonPattern(pattern,
 *         log::param("trace_id", traceId));
 * @endcode
 *
 * @tparam Ts  Value types of the parameters.
 * @param existingPattern  A pattern previously produced by build() or an
 *                         empty string to start fresh.
 * @param params  The parameters to add as JSON fields.
 * @return The spdlog-compatible JSON pattern string.
 */
template <typename... Ts>
[[nodiscard]] std::string
buildJsonPattern(std::string_view existingPattern, log::Parameter<Ts> const&... params)
{
    // NOLINTNEXTLINE(misc-const-correctness)
    detail::JsonLoggingPatternBuilder builder(existingPattern);
    (builder.add(params.name(), params.value()), ...);
    return builder.build();
}

}  // namespace xrpl
