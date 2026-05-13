/** @file
 *  Implements SemanticVersion parsing, printing, and comparison.
 *
 *  Provides a strict, spec-compliant parser for Semantic Versioning 2.0
 *  strings. Inputs that deviate from the specification in any way — including
 *  leading zeroes, embedded whitespace, or trailing characters — are rejected.
 *  The file-scope helpers below are internal to this translation unit.
 *
 *  @see https://semver.org/
 */

#include <xrpl/beast/core/SemanticVersion.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <locale>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beast {

/** Serialize an identifier list to a dot-separated string.
 *
 *  Joins each element of @p list with a `.` separator, producing the
 *  canonical SemVer pre-release or build-metadata segment (without the
 *  leading `-` or `+` sigil).
 *
 *  @param list The identifier list to serialize. May be empty, in which
 *      case an empty string is returned.
 *  @return A dot-separated concatenation of all identifiers.
 */
std::string
printIdentifiers(SemanticVersion::identifier_list const& list)
{
    std::string ret;

    for (auto const& x : list)
    {
        if (!ret.empty())
            ret += ".";
        ret += x;
    }

    return ret;
}

/** Determine whether a pre-release identifier string is purely numeric.
 *
 *  A string is numeric in the SemVer sense if it converts to a non-negative
 *  integer and has no leading zeroes. The leading-zero check is performed by
 *  round-tripping: `lexicalCastChecked("01", n)` succeeds with `n = 1`, but
 *  `std::to_string(1) != "01"` catches the problem without explicit string
 *  inspection.
 *
 *  @param s The identifier to test.
 *  @return `true` if @p s represents a non-negative integer with no leading
 *      zeroes; `false` otherwise.
 */
bool
isNumeric(std::string const& s)
{
    int n = 0;

    if (!lexicalCastChecked(n, s))
        return false;

    return std::to_string(n) == s;
}

/** Consume a fixed literal prefix from a mutable input string.
 *
 *  If @p input begins with @p what, erases those characters from the front
 *  of @p input and returns `true`. If the prefix is not present, @p input
 *  is left unchanged and `false` is returned.
 *
 *  @param what The literal prefix to match and consume.
 *  @param input The string being parsed; modified in place on success.
 *  @return `true` if @p what was found at position 0 and consumed.
 */
bool
chop(std::string const& what, std::string& input)
{
    auto ret = input.find(what);

    if (ret != 0)
        return false;

    input.erase(0, what.size());
    return true;
}

/** Consume a non-negative integer from the front of a mutable input string.
 *
 *  Scans leading digit characters from @p input, converts them to an `int`,
 *  and writes the result into @p value. The consumed digits are erased from
 *  @p input on success.
 *
 *  Enforces three invariants beyond simple conversion:
 *  - The digit run must not be empty.
 *  - The value must have no leading zeroes (detected via round-trip through
 *    `std::to_string`; e.g. `"01"` converts to `1` but `"1" != "01"` fails).
 *  - The value must be in the range `[0, limit]`.
 *
 *  @param value Receives the parsed integer on success.
 *  @param limit The inclusive upper bound for the parsed value.
 *  @param input The string being parsed; the consumed digits are erased on
 *      success.
 *  @return `true` if a valid integer was consumed; `false` otherwise, with
 *      @p input and @p value left unmodified.
 */
bool
chopUInt(int& value, int limit, std::string& input)
{
    if (input.empty())
        return false;

    auto leftIter = std::ranges::find_if_not(
        input, [](std::string::value_type c) { return std::isdigit(c, std::locale::classic()); });

    std::string const item(input.begin(), leftIter);

    if (item.empty())
        return false;

    int n = 0;

    if (!lexicalCastChecked(n, item))
        return false;

    if (std::to_string(n) != item)
        return false;

    if (n < 0 || n > limit)
        return false;

    input.erase(input.begin(), leftIter);
    value = n;

    return true;
}

/** Consume a single SemVer identifier token from the front of a mutable string.
 *
 *  An identifier is a maximal run of characters from the SemVer-allowed set
 *  `[a-zA-Z0-9-]`. The consumed token is written to @p value and erased from
 *  @p input on success.
 *
 *  The @p allowLeadingZeroes flag reflects an asymmetry in the SemVer spec:
 *  pre-release identifiers must not begin with `'0'` (e.g. `"01"` is
 *  illegal), while build-metadata identifiers have no such restriction. Pass
 *  `false` for pre-release identifiers and `true` for build metadata.
 *
 *  @param value Receives the extracted identifier token on success.
 *  @param allowLeadingZeroes If `false`, rejects tokens whose first character
 *      is `'0'`.
 *  @param input The string being parsed; the consumed token is erased on
 *      success.
 *  @return `true` if a non-empty valid identifier was consumed; `false`
 *      otherwise, with @p input and @p value left unmodified.
 */
bool
extractIdentifier(std::string& value, bool allowLeadingZeroes, std::string& input)
{
    if (input.empty())
        return false;

    if (!allowLeadingZeroes && input[0] == '0')
        return false;

    auto last =
        input.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");

    if (last == 0)
        return false;

    value = input.substr(0, last);
    input.erase(0, last);
    return true;
}

/** Consume a dot-separated list of SemVer identifiers from a mutable string.
 *
 *  Repeatedly calls `extractIdentifier` to build a list, consuming `.`
 *  separators between tokens. Requires at least one identifier to be present.
 *  The @p allowLeadingZeroes flag is forwarded to each `extractIdentifier`
 *  call; pass `false` for pre-release lists and `true` for build metadata.
 *
 *  @param identifiers Receives the parsed identifier tokens, appended on
 *      success.
 *  @param allowLeadingZeroes Forwarded to `extractIdentifier` for each token.
 *  @param input The string being parsed; consumed tokens and separators are
 *      erased on success.
 *  @return `true` if at least one valid identifier was consumed; `false` if
 *      the input is empty or any token fails validation.
 */
bool
extractIdentifiers(
    SemanticVersion::identifier_list& identifiers,
    bool allowLeadingZeroes,
    std::string& input)
{
    if (input.empty())
        return false;

    do
    {
        std::string s;

        if (!extractIdentifier(s, allowLeadingZeroes, input))
            return false;
        identifiers.push_back(s);
    } while (chop(".", input));

    return true;
}

SemanticVersion::SemanticVersion() : majorVersion(0), minorVersion(0), patchVersion(0)
{
}

/** Construct and parse a SemanticVersion, throwing on failure.
 *
 *  Delegates to `parse()`. Use this constructor when an invalid version
 *  string is a programming error; use `parse()` directly when the caller
 *  needs to handle invalid input without exceptions (e.g., config loading).
 *
 *  @param version The version string to parse.
 *  @throws std::invalid_argument if @p version does not conform to the
 *      Semantic Versioning 2.0 specification.
 */
SemanticVersion::SemanticVersion(std::string_view version) : SemanticVersion()
{
    if (!parse(version))
        throw std::invalid_argument("invalid version string");
}

bool
SemanticVersion::parse(std::string_view input)
{
    auto leftIter = std::ranges::find_if_not(
        input, [](std::string::value_type c) { return std::isspace(c, std::locale::classic()); });

    auto rightIter =
        std::ranges::find_if_not(std::ranges::reverse_view(input), [](std::string::value_type c) {
            return std::isspace(c, std::locale::classic());
        }).base();

    if (leftIter >= rightIter)
        return false;

    std::string version(leftIter, rightIter);

    // Reject any leading or trailing whitespace.
    if (version != input)
        return false;

    if (!chopUInt(majorVersion, std::numeric_limits<int>::max(), version))
        return false;
    if (!chop(".", version))
        return false;

    if (!chopUInt(minorVersion, std::numeric_limits<int>::max(), version))
        return false;
    if (!chop(".", version))
        return false;

    if (!chopUInt(patchVersion, std::numeric_limits<int>::max(), version))
        return false;

    if (chop("-", version))
    {
        if (!extractIdentifiers(preReleaseIdentifiers, false, version))
            return false;

        if (preReleaseIdentifiers.empty())
            return false;
    }

    if (chop("+", version))
    {
        if (!extractIdentifiers(metaData, true, version))
            return false;

        if (metaData.empty())
            return false;
    }

    // Any unconsumed characters indicate an unrecognised token.
    return version.empty();
}

std::string
SemanticVersion::print() const
{
    std::string s;

    s = std::to_string(majorVersion) + "." + std::to_string(minorVersion) + "." +
        std::to_string(patchVersion);

    if (!preReleaseIdentifiers.empty())
    {
        s += "-";
        s += printIdentifiers(preReleaseIdentifiers);
    }

    if (!metaData.empty())
    {
        s += "+";
        s += printIdentifiers(metaData);
    }

    return s;
}

/** Compare two SemanticVersion objects following SemVer 2.0 precedence rules.
 *
 *  Comparison proceeds in order: major, minor, patch (numerically), then
 *  pre-release status. When the numeric cores are equal, a release version
 *  (no pre-release identifiers) always outranks a pre-release — e.g.
 *  `1.0.0 > 1.0.0-rc.1`. Pre-release identifier lists are then compared
 *  element by element: purely numeric identifiers compare as integers;
 *  alphanumeric identifiers compare lexicographically; a mixed pair (one
 *  numeric, one not) ranks the non-numeric higher. When one list is
 *  exhausted before the other, the longer list takes precedence.
 *
 *  @note Build metadata is ignored entirely, as mandated by the spec.
 *      `1.0.0+meta == 1.0.0` under this function.
 *  @note `XRPL_ASSERT` guards the consistency between `isNumeric()` and the
 *      comparison branches; they fire only on internal logic bugs.
 *
 *  @param lhs Left-hand operand.
 *  @param rhs Right-hand operand.
 *  @return Negative if `lhs < rhs`, zero if equal, positive if `lhs > rhs`.
 */
int
compare(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    if (lhs.majorVersion > rhs.majorVersion)
        return 1;
    if (lhs.majorVersion < rhs.majorVersion)
        return -1;

    if (lhs.minorVersion > rhs.minorVersion)
        return 1;
    if (lhs.minorVersion < rhs.minorVersion)
        return -1;

    if (lhs.patchVersion > rhs.patchVersion)
        return 1;
    if (lhs.patchVersion < rhs.patchVersion)
        return -1;

    if (lhs.isPreRelease() || rhs.isPreRelease())
    {
        if (lhs.isRelease() && rhs.isPreRelease())
            return 1;
        if (lhs.isPreRelease() && rhs.isRelease())
            return -1;

        for (int i = 0;
             i < std::max(lhs.preReleaseIdentifiers.size(), rhs.preReleaseIdentifiers.size());
             ++i)
        {
            if (i >= rhs.preReleaseIdentifiers.size())
                return 1;
            if (i >= lhs.preReleaseIdentifiers.size())
                return -1;

            std::string const& left(lhs.preReleaseIdentifiers[i]);
            std::string const& right(rhs.preReleaseIdentifiers[i]);

            if (!isNumeric(left) && isNumeric(right))
                return 1;
            if (isNumeric(left) && !isNumeric(right))
                return -1;

            if (isNumeric(left))
            {
                XRPL_ASSERT(isNumeric(right), "beast::compare : both inputs numeric");

                int const iLeft(lexicalCastThrow<int>(left));
                int const iRight(lexicalCastThrow<int>(right));

                if (iLeft > iRight)
                    return 1;
                if (iLeft < iRight)
                    return -1;
            }
            else
            {
                XRPL_ASSERT(!isNumeric(right), "beast::compare : both inputs non-numeric");

                int const result = left.compare(right);

                if (result != 0)
                    return result;
            }
        }
    }

    return 0;
}

}  // namespace beast
