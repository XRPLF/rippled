#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace beast {

/** A Semantic Version number.

    Identifies the build of a particular version of software using
    the Semantic Versioning Specification described here:

    http://semver.org/
*/
class SemanticVersion
{
public:
    using identifier_list = std::vector<std::string>;

    int majorVersion;
    int minorVersion;
    int patchVersion;

    identifier_list preReleaseIdentifiers;
    identifier_list metaData;

    SemanticVersion();

    SemanticVersion(std::string_view version);

    /** Parse a semantic version string.
        The parsing is as strict as possible.
        @return `true` if the string was parsed.
    */
    bool
    parse(std::string_view input);

    /** Produce a string from semantic version components. */
    [[nodiscard]] std::string
    print() const;

    [[nodiscard]] bool
    isRelease() const noexcept
    {
        return preReleaseIdentifiers.empty();
    }
    [[nodiscard]] bool
    isPreRelease() const noexcept
    {
        return !isRelease();
    }
};

/** Compare two SemanticVersions against each other.
    The comparison follows the rules as per the specification.
*/
int
compare(SemanticVersion const& lhs, SemanticVersion const& rhs);

inline bool
operator==(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) == 0;
}

inline bool
operator!=(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) != 0;
}

inline bool
operator>=(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) >= 0;
}

inline bool
operator<=(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) <= 0;
}

inline bool
operator>(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) > 0;
}

inline bool
operator<(SemanticVersion const& lhs, SemanticVersion const& rhs)
{
    return compare(lhs, rhs) < 0;
}

}  // namespace beast
