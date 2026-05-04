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

bool
isNumeric(std::string const& s)
{
    int n = 0;

    // Must be convertible to an integer
    if (!lexicalCastChecked(n, s))
        return false;

    // Must not have leading zeroes
    return std::to_string(n) == s;
}

bool
chop(std::string const& what, std::string& input)
{
    auto ret = input.find(what);

    if (ret != 0)
        return false;

    input.erase(0, what.size());
    return true;
}

bool
chopUInt(int& value, int limit, std::string& input)
{
    // Must not be empty
    if (input.empty())
        return false;

    auto leftIter = std::ranges::find_if_not(
        input, [](std::string::value_type c) { return std::isdigit(c, std::locale::classic()); });

    std::string const item(input.begin(), leftIter);

    // Must not be empty
    if (item.empty())
        return false;

    int n = 0;

    // Must be convertible to an integer
    if (!lexicalCastChecked(n, item))
        return false;

    // Must not have leading zeroes
    if (std::to_string(n) != item)
        return false;

    // Must not be out of range
    if (n < 0 || n > limit)
        return false;

    input.erase(input.begin(), leftIter);
    value = n;

    return true;
}

bool
extractIdentifier(std::string& value, bool allowLeadingZeroes, std::string& input)
{
    // Must not be empty
    if (input.empty())
        return false;

    // Must not have a leading 0
    if (!allowLeadingZeroes && input[0] == '0')
        return false;

    auto last =
        input.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");

    // Must not be empty
    if (last == 0)
        return false;

    value = input.substr(0, last);
    input.erase(0, last);
    return true;
}

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

//------------------------------------------------------------------------------

SemanticVersion::SemanticVersion() : majorVersion(0), minorVersion(0), patchVersion(0)
{
}

SemanticVersion::SemanticVersion(std::string_view version) : SemanticVersion()
{
    if (!parse(version))
        throw std::invalid_argument("invalid version string");
}

bool
SemanticVersion::parse(std::string_view input)
{
    // May not have leading or trailing whitespace
    auto leftIter = std::ranges::find_if_not(
        input, [](std::string::value_type c) { return std::isspace(c, std::locale::classic()); });

    auto rightIter =
        std::ranges::find_if_not(std::ranges::reverse_view(input), [](std::string::value_type c) {
            return std::isspace(c, std::locale::classic());
        }).base();

    // Must not be empty!
    if (leftIter >= rightIter)
        return false;

    std::string version(leftIter, rightIter);

    // May not have leading or trailing whitespace
    if (version != input)
        return false;

    // Must have major version number
    if (!chopUInt(majorVersion, std::numeric_limits<int>::max(), version))
        return false;
    if (!chop(".", version))
        return false;

    // Must have minor version number
    if (!chopUInt(minorVersion, std::numeric_limits<int>::max(), version))
        return false;
    if (!chop(".", version))
        return false;

    // Must have patch version number
    if (!chopUInt(patchVersion, std::numeric_limits<int>::max(), version))
        return false;

    // May have pre-release identifier list
    if (chop("-", version))
    {
        if (!extractIdentifiers(preReleaseIdentifiers, false, version))
            return false;

        // Must not be empty
        if (preReleaseIdentifiers.empty())
            return false;
    }

    // May have metadata identifier list
    if (chop("+", version))
    {
        if (!extractIdentifiers(metaData, true, version))
            return false;

        // Must not be empty
        if (metaData.empty())
            return false;
    }

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

int
compare(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    if (lhs.majorVersion > rhs.majorVersion)
    {
        return 1;
    }
    if (lhs.majorVersion < rhs.majorVersion)
    {
        return -1;
    }

    if (lhs.minorVersion > rhs.minorVersion)
    {
        return 1;
    }
    if (lhs.minorVersion < rhs.minorVersion)
    {
        return -1;
    }

    if (lhs.patchVersion > rhs.patchVersion)
    {
        return 1;
    }
    if (lhs.patchVersion < rhs.patchVersion)
    {
        return -1;
    }

    if (lhs.isPreRelease() || rhs.isPreRelease())
    {
        // Pre-releases have a lower precedence
        if (lhs.isRelease() && rhs.isPreRelease())
        {
            return 1;
        }
        if (lhs.isPreRelease() && rhs.isRelease())
        {
            return -1;
        }

        // Compare pre-release identifiers
        for (int i = 0;
             i < std::max(lhs.preReleaseIdentifiers.size(), rhs.preReleaseIdentifiers.size());
             ++i)
        {
            // A larger list of identifiers has a higher precedence
            if (i >= rhs.preReleaseIdentifiers.size())
            {
                return 1;
            }
            if (i >= lhs.preReleaseIdentifiers.size())
            {
                return -1;
            }

            std::string const& left(lhs.preReleaseIdentifiers[i]);
            std::string const& right(rhs.preReleaseIdentifiers[i]);

            // Numeric identifiers have lower precedence
            if (!isNumeric(left) && isNumeric(right))
            {
                return 1;
            }
            if (isNumeric(left) && !isNumeric(right))
            {
                return -1;
            }

            if (isNumeric(left))
            {
                XRPL_ASSERT(isNumeric(right), "beast::compare : both inputs numeric");

                int const iLeft(lexicalCastThrow<int>(left));
                int const iRight(lexicalCastThrow<int>(right));

                if (iLeft > iRight)
                {
                    return 1;
                }
                if (iLeft < iRight)
                {
                    return -1;
                }
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

    // metadata is ignored

    return 0;
}

}  // namespace beast
