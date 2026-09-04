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
 * A view is only usable that way if it covers a whole null-terminated string
 * rather than a slice of one, since a reader given just the pointer stops at
 * the first null. This asks exactly that question: rebuild the view from its
 * data() as a C string, as such a reader would, and see if it comes back
 * unchanged. A slice comes back longer, having run past its own end.
 *
 * The byte after a view is not part of it, so this is only well defined when
 * @p str points into storage that is known to hold a null somewhere at or
 * after its end -- a string literal, or the buffer of a std::string. For a
 * view built from a bare pointer and length it reads out of bounds, and in a
 * constant expression that is a compile error rather than undefined behaviour.
 *
 * @param str The view to test.
 * @return Whether @p str is null-terminated.
 */
constexpr bool
isNullTerminated(std::string_view str)
{
    // Reading past the view is the point, so the usual warning about data() not
    // being null-terminated does not apply.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    return std::string_view{str.data()} == str;
}

}  // namespace xrpl
