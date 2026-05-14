/**
 * @file BuildInfo.cpp
 * @brief Version identity, wire encoding, and peer-comparison utilities for xrpld.
 *
 * Owns the single source-of-truth version string (`versionString`), its
 * transformation into a SemVer-validated public string, the composite
 * `systemName-version` identifier used in HTTP headers and peer handshakes,
 * and the 64-bit wire encoding that allows integer comparison of peer versions
 * during consensus without string parsing.
 *
 * Every flag ledger (every 256 ledgers), `RCLConsensus` writes the result of
 * `getEncodedVersion()` into `sfServerVersion` in each validation message.
 * `LedgerMaster` then tallies those values with `isXrpldVersion()` and
 * `isNewerVersion()` to detect when a significant fraction of the UNL is
 * running newer software and emit an upgrade notification.
 */

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

/** The release version string — the single edit point when cutting a release.
 *
 *  Must be a valid Semantic Version (http://semver.org/) in canonical form.
 *  `getVersionString()` validates this at startup via a round-trip through
 *  `beast::SemanticVersion`; a malformed or non-canonical string triggers
 *  `logicError` and kills the process immediately.
 */
// clang-format off
// NOLINTNEXTLINE(readability-identifier-naming)
char const* const versionString = "3.2.0-b0"
    // clang-format on
    ;

/** Build the version string, optionally appending SemVer build metadata.
 *
 *  In `DEBUG` or sanitizer builds, metadata is appended as a `+`-separated
 *  suffix following SemVer §10.  The metadata components are, in order:
 *    - the short Git commit hash (if available from `xrpl::git::getCommitHash()`),
 *    - the literal token `DEBUG` (when `DEBUG` is defined),
 *    - the stringified value of the `SANITIZERS` preprocessor macro (e.g.
 *      `address,undefined` when `-DSANITIZERS=address,undefined` is passed).
 *
 *  `BOOST_PP_STRINGIZE` is used to convert the token list supplied via
 *  `-DSANITIZERS=...` into a runtime string; there is no other way to
 *  capture an arbitrary preprocessor token sequence as a string literal.
 *
 *  In release (non-DEBUG, non-sanitizer) builds this simply returns
 *  `versionString` unchanged.
 *
 *  @return the version string, with build metadata appended when applicable.
 */
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

/** Return the validated SemVer version string for this build.
 *
 *  Memoized via a function-local `static const`; the initializer runs exactly
 *  once (C++11 guarantee, thread-safe).  On first call, the string produced by
 *  `buildVersionString()` is round-tripped through `beast::SemanticVersion`:
 *  if parsing fails, or if the canonical re-serialization (`v.print()`) differs
 *  from the original, `logicError` is called and the process terminates.  This
 *  catches both malformed strings and strings that are semantically valid but
 *  not in canonical form (e.g., superfluous leading zeros in a version
 *  component).
 *
 *  @return a reference to the cached, validated version string (e.g. `"3.2.0-b0"`).
 */
std::string const&
getVersionString()
{
    static std::string const kVALUE = [] {
        std::string const s = buildVersionString();

        beast::SemanticVersion v;
        if (!v.parse(s) || v.print() != s)
            logicError(s + ": Bad server version string");
        return s;
    }();
    return kVALUE;
}

/** Return the composite `systemName-version` identifier for this build.
 *
 *  Prepends `systemName()` (always `"xrpld"`) to `getVersionString()`,
 *  separated by `"-"`, e.g. `"xrpld-3.2.0-b0"`.  This form is used in
 *  the `User-Agent` header of outbound HTTP requests and in startup log
 *  messages.  Memoized on first call.
 *
 *  @return a reference to the cached composite version string.
 */
std::string const&
getFullVersionString()
{
    static std::string const kVALUE = systemName() + "-" + getVersionString();
    return kVALUE;
}

/** Upper-16-bit fingerprint that identifies an encoded version as xrpld.
 *
 *  Any `uint64_t` whose top 16 bits equal this constant was produced by
 *  `encodeSoftwareVersion()` in this implementation.  Checked by
 *  `isXrpldVersion()` before any numeric comparison, preventing a
 *  non-xrpld peer from appearing "newer" through a spuriously large integer.
 */
static constexpr std::uint64_t kIMPLEMENTATION_VERSION_IDENTIFIER = 0x183B'0000'0000'0000LLU;

/** Bitmask to extract the upper 16 bits of an encoded version for fingerprint comparison. */
static constexpr std::uint64_t kIMPLEMENTATION_VERSION_IDENTIFIER_MASK = 0xFFFF'0000'0000'0000LLU;

/** Compress a SemVer string into the 64-bit wire encoding described in `BuildInfo.h`.
 *
 *  The `TYPE` field uses a deliberate encoding so that a plain integer comparison
 *  on the resulting `uint64_t` yields correct semantic ordering across pre-release
 *  types: `0b11` (release) > `0b10` (RC) > `0b01` (beta).
 *
 *  Pre-release parsing is performed by an internal `parsePreRelease` lambda that:
 *    - checks for a `"rc"` or `"b"` prefix (case-sensitive),
 *    - converts the numeric suffix via `beast::lexicalCastChecked` (safe, no throw),
 *    - clamps the result to [0, 63] — the 6-bit `N` field.
 *  Any malformed pre-release identifier (empty suffix, non-numeric, out-of-range)
 *  silently yields zero for the pre-release byte, which sorts below any known type.
 *
 *  If `versionStr` does not parse as a valid SemVer string, the return value
 *  contains only the xrpld fingerprint (`0x183B`) in the upper 16 bits and
 *  zeros elsewhere.
 *
 *  @param versionStr a SemVer-formatted version string (e.g. `"3.2.0-b0"`).
 *  @return the encoded version as a `uint64_t`; see `BuildInfo.h` for bit layout.
 */
std::uint64_t
encodeSoftwareVersion(std::string_view versionStr)
{
    std::uint64_t c = kIMPLEMENTATION_VERSION_IDENTIFIER;

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
                // Returns `number + key` when `identifier` begins with `prefix`
                // and its numeric suffix is in [lok, hik]; returns 0 on any failure.
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

/** Return this node's own version packed in the 64-bit wire encoding.
 *
 *  Lazily encodes `getVersionString()` on first call and caches the result as a
 *  function-local static.  Written into `sfServerVersion` in every validation
 *  message emitted on flag ledgers (every 256 ledgers) by `RCLConsensus`.
 *
 *  @return the cached encoded version for this build.
 */
std::uint64_t
getEncodedVersion()
{
    static std::uint64_t const kCOOKIE = {encodeSoftwareVersion(getVersionString())};
    return kCOOKIE;
}

/** Return true if `version` carries the xrpld implementation fingerprint.
 *
 *  Checks only the upper 16 bits against `kIMPLEMENTATION_VERSION_IDENTIFIER`
 *  (`0x183B`).  Must be called before any numeric comparison of version values
 *  to guard against non-xrpld peers advertising an arbitrarily large integer
 *  that would otherwise appear "newer".
 *
 *  @param version an encoded software version read from `sfServerVersion`.
 *  @return true iff the upper 16 bits equal `0x183B`.
 */
bool
isXrpldVersion(std::uint64_t version)
{
    return (version & kIMPLEMENTATION_VERSION_IDENTIFIER_MASK) ==
        kIMPLEMENTATION_VERSION_IDENTIFIER;
}

/** Return true if `version` represents a newer xrpld release than this node.
 *
 *  Guards against non-xrpld peers by calling `isXrpldVersion()` first;
 *  any value whose upper 16 bits differ from `0x183B` unconditionally
 *  returns false regardless of its numeric magnitude.  For confirmed xrpld
 *  versions, a plain integer comparison is sufficient because
 *  `encodeSoftwareVersion()` encodes major/minor/patch and the release-type
 *  bits in descending significance order.
 *
 *  @param version an encoded software version read from `sfServerVersion`.
 *  @return true iff `version` is an xrpld version strictly greater than
 *      `getEncodedVersion()`.
 */
bool
isNewerVersion(std::uint64_t version)
{
    if (isXrpldVersion(version))
        return version > getEncodedVersion();
    return false;
}

}  // namespace xrpl::BuildInfo
