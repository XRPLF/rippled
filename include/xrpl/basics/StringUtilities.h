#pragma once

#include <xrpl/basics/Blob.h>

#include <boost/utility/string_view.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * Format arbitrary binary data as an SQLite "blob literal".
 *
 * In SQLite, blob literals must be encoded when used in a query. Per
 * https://sqlite.org/lang_expr.html#literal_values_constants_ they are
 * encoded as string literals containing hexadecimal data and preceded
 * by a single 'X' character.
 *
 * @param blob An arbitrary blob of binary data
 * @return The input, encoded as a blob literal.
 */
std::string
sqlBlobLiteral(Blob const& blob);

namespace detail {

template <typename T>
concept SomeChar = std::same_as<std::remove_cvref_t<T>, int8_t> ||
    std::same_as<std::remove_cvref_t<T>, char> || std::same_as<std::remove_cvref_t<T>, uint8_t>;

inline constexpr std::array<std::optional<int>, 256> const kDigitLookupTable = []() {
    std::array<std::optional<int>, 256> t{};

    for (int i = 0; i < 10; ++i)
        t['0' + i] = i;

    for (int i = 0; i < 6; ++i)
    {
        t['A' + i] = 10 + i;
        t['a' + i] = 10 + i;
    }

    return t;
}();

inline std::optional<int>
hexCharToInt(SomeChar auto hexChar)
{
    return kDigitLookupTable[static_cast<uint8_t>(hexChar)];
}

}  // namespace detail

template <class Iterator>
std::optional<Blob>
strUnHex(std::size_t strSize, Iterator begin, Iterator end)
{
    Blob out;

    out.reserve((strSize + 1) / 2);

    auto iter = begin;

    if (strSize & 1)
    {
        auto const c = detail::hexCharToInt(*iter++);
        if (!c.has_value())
            return {};

        out.push_back(static_cast<unsigned char>(*c));
    }

    while (iter != end)
    {
        auto const cHigh = detail::hexCharToInt(*iter++);

        if (!cHigh.has_value())
            return {};

        auto const cLow = detail::hexCharToInt(*iter++);

        if (!cLow.has_value())
            return {};

        out.push_back(static_cast<unsigned char>((*cHigh << 4) | *cLow));
    }

    return {std::move(out)};
}

inline std::optional<Blob>
strUnHex(std::string_view strSrc)
{
    return strUnHex(strSrc.size(), strSrc.cbegin(), strSrc.cend());
}

struct ParsedUrl
{
    explicit ParsedUrl() = default;

    std::string scheme;
    std::string username;
    std::string password;
    std::string domain;
    std::optional<std::uint16_t> port;
    std::string path;

    bool
    operator==(ParsedUrl const& other) const
    {
        return scheme == other.scheme && domain == other.domain && port == other.port &&
            path == other.path;
    }
};

bool
parseUrl(ParsedUrl& pUrl, std::string const& strUrl);

/**
 * Remove leading and trailing ASCII whitespace.
 *
 * Whitespace is the fixed set " \t\n\v\f\r"; the current locale is not
 * consulted, so the result depends only on the input.
 *
 * @param str The string to trim.
 * @return @p str without leading or trailing whitespace.
 */
std::string
trimWhitespace(std::string str);

/**
 * Fold ASCII upper case letters to lower case.
 *
 * Only 'A' through 'Z' are remapped; every other byte is left alone and the
 * current locale is not consulted, so the result depends only on the input.
 *
 * @param str The string to fold.
 * @return @p str with each ASCII upper case letter replaced by its lower case
 *         equivalent.
 */
std::string
toLower(std::string str);

std::optional<std::uint64_t>
toUInt64(std::string const& s);

/**
 * Determines if the given string looks like a TOML-file hosting domain.
 *
 * Do not use this function to determine if a particular string is a valid
 * domain, as this function may reject domains that are otherwise valid and
 * doesn't check whether the TLD is valid.
 */
bool
isProperlyFormedTomlDomain(std::string_view domain);

/**
 * Whether a view can be passed on as a C string.
 *
 * A reader given only data() stops at the first null, so the view must reach the
 * terminating null. The test rebuilds the view from data() and compares: a view
 * that stops earlier rebuilds longer, and so compares unequal.
 *
 * consteval because reading the byte after the view is only defined when @p str
 * points into storage holding a null at or after its end, such as a string
 * literal. An unterminated view is then a compile error, not an out-of-bounds
 * read.
 *
 * @param str The view to test.
 * @return Whether @p str is null-terminated. A view with no data is not.
 */
consteval bool
isNullTerminated(std::string_view str)
{
    if (str.data() == nullptr)
        return false;

    // Reading past the view is the point, so the usual data() warning does not
    // apply.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    return std::string_view{str.data()} == str;
}

/**
 * A string that is known to reach its terminating null.
 *
 * Converts to std::string_view, so it compares and hashes as one. Unlike a
 * view, asCString() may be handed to a reader that expects a C string, such
 * as json::StaticString.
 *
 * The only constructor is consteval and rejects a view that stops before the
 * null, so the property holds by construction and no caller asserts it.
 */
class NullTerminatedView
{
public:
    /**
     * Build a view from one that reaches its terminating null.
     *
     * @param view The string to hold. Rejected at compile time if it stops
     *        before its terminating null, or has no data.
     */
    consteval NullTerminatedView(std::string_view view) : data_(view.data()), size_(view.size())
    {
        if (!isNullTerminated(view))
            throw "xrpl::NullTerminatedView : view does not reach a null";
    }

    constexpr
    operator std::string_view() const noexcept
    {
        return view();
    }

    /**
     * @return The string as a view.
     */
    [[nodiscard]] constexpr std::string_view
    view() const noexcept
    {
        return {data_, size_};
    }

    /**
     * @return The string as a C string. Never null.
     */
    [[nodiscard]] constexpr char const*
    asCString() const noexcept
    {
        return data_;
    }

private:
    char const* data_;
    std::size_t size_;
};

}  // namespace xrpl
