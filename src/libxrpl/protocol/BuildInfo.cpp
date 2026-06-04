#include <xrpl/protocol/BuildInfo.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/git/Git.h>  // IWYU pragma: keep
#include <xrpl/protocol/SystemParameters.h>

#include <boost/preprocessor/stringize.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::BuildInfo {

namespace {

//--------------------------------------------------------------------------
//  The build version number. You must edit this for each release
//  and follow the format described at http://semver.org/
//------------------------------------------------------------------------------
// clang-format off
// NOLINTNEXTLINE(readability-identifier-naming)
char const* const versionString = "3.2.0-rc3"
    // clang-format on
    ;

//
// Don't touch anything below this line
//

std::string
buildVersionString()
{
    std::string version = versionString;

#if defined(DEBUG) || defined(SANITIZERS)
    std::string metadata;

    std::string const& commitHash = xrpl::git::getCommitHash();
    if (!commitHash.empty())
        metadata += commitHash + ".";

#ifdef DEBUG
    metadata += "DEBUG";
#endif

#if defined(DEBUG) && defined(SANITIZERS)
    metadata += ".";
#endif

#ifdef SANITIZERS
    metadata += BOOST_PP_STRINGIZE(SANITIZERS);  // cspell: disable-line
#endif

    if (!metadata.empty())
        version += "+" + metadata;
#endif

    return version;
}

}  // namespace

std::string const&
getVersionString()
{
    static std::string const kValue = [] {
        std::string const s = buildVersionString();

        beast::SemanticVersion v;
        if (!v.parse(s) || v.print() != s)
            logicError(s + ": Bad server version string");
        return s;
    }();
    return kValue;
}

std::string const&
getFullVersionString()
{
    static std::string const kValue = systemName() + "-" + getVersionString();
    return kValue;
}

static constexpr std::uint64_t kImplementationVersionIdentifier = 0x183B'0000'0000'0000LLU;
static constexpr std::uint64_t kImplementationVersionIdentifierMask = 0xFFFF'0000'0000'0000LLU;

std::uint64_t
encodeSoftwareVersion(std::string_view versionStr)
{
    std::uint64_t c = kImplementationVersionIdentifier;

    beast::SemanticVersion v;

    if (v.parse(versionStr))
    {
        if (v.majorVersion >= 0 && v.majorVersion <= 255)
            c |= static_cast<std::uint64_t>(v.majorVersion) << 40;

        if (v.minorVersion >= 0 && v.minorVersion <= 255)
            c |= static_cast<std::uint64_t>(v.minorVersion) << 32;

        if (v.patchVersion >= 0 && v.patchVersion <= 255)
            c |= static_cast<std::uint64_t>(v.patchVersion) << 24;

        if (!v.isPreRelease())
            c |= static_cast<std::uint64_t>(0xC00000);

        if (v.isPreRelease())
        {
            std::uint8_t x = 0;

            for (auto const& id : v.preReleaseIdentifiers)
            {
                auto parsePreRelease = [](std::string_view identifier,
                                          std::string_view prefix,
                                          std::uint8_t key,
                                          std::uint8_t lok,
                                          std::uint8_t hik) -> std::uint8_t {
                    std::uint8_t ret = 0;

                    if (!identifier.starts_with(prefix))
                        return 0;

                    if (!beast::lexicalCastChecked(
                            ret, std::string(identifier.substr(prefix.length()))))
                        return 0;

                    if (std::clamp(ret, lok, hik) != ret)
                        return 0;

                    return ret + key;
                };

                x = parsePreRelease(id, "rc", 0x80, 0, 63);

                if (x == 0)
                    x = parsePreRelease(id, "b", 0x40, 0, 63);

                if ((x & 0xC0) != 0)
                {
                    c |= static_cast<std::uint64_t>(x) << 16;
                    break;
                }
            }
        }
    }

    return c;
}

std::uint64_t
getEncodedVersion()
{
    static std::uint64_t const kCookie = {encodeSoftwareVersion(getVersionString())};
    return kCookie;
}

bool
isXrpldVersion(std::uint64_t version)
{
    return (version & kImplementationVersionIdentifierMask) == kImplementationVersionIdentifier;
}

bool
isNewerVersion(std::uint64_t version)
{
    if (isXrpldVersion(version))
        return version > getEncodedVersion();
    return false;
}

}  // namespace xrpl::BuildInfo
