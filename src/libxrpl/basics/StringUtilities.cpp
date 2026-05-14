/** @file
 *  Implements string and binary-data utilities used across the XRPL daemon.
 *
 *  Provides five free functions in the `xrpl` namespace: `sqlBlobLiteral`
 *  for SQLite hex-literal encoding, `parseUrl` for URI decomposition,
 *  `trimWhitespace` for whitespace stripping, `toUInt64` for safe decimal
 *  conversion, and `isProperlyFormedTomlDomain` for validator-TOML domain
 *  validation.  Both regex objects are `static` locals so they are compiled
 *  once and reused across all calls.
 */
#include <xrpl/basics/StringUtilities.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace xrpl {

/** Encode binary data as an SQLite blob literal of the form `X'AABBCC...'`.
 *
 *  Pre-reserves `(blob.size() * 2) + 3` bytes to hold the leading `X'`, the
 *  hex body, and the closing `'` without reallocation.
 *
 *  @param blob Binary data to encode.
 *  @return The input encoded as an SQLite blob literal.
 *  @see https://sqlite.org/lang_expr.html#literal_values_constants_
 */
std::string
sqlBlobLiteral(Blob const& blob)
{
    std::string j;

    j.reserve((blob.size() * 2) + 3);
    j.push_back('X');
    j.push_back('\'');
    boost::algorithm::hex(blob.begin(), blob.end(), std::back_inserter(j));
    j.push_back('\'');

    return j;
}

/** Decompose a URI into its constituent parts, populating a `ParsedUrl`.
 *
 *  Only URIs with an `//authority` hier-part are accepted
 *  (`scheme://[userinfo@]host[:port][/path]`).  Opaque URIs such as
 *  `mailto:user@example.com` are rejected.  The extracted scheme is always
 *  stored in lower-case regardless of the case in `strUrl`.
 *
 *  IPv6 addresses in bracket notation (e.g. `[::1]`) are normalised: the
 *  brackets are stripped and the address is canonicalised via
 *  `beast::IP::Endpoint`.  Plain hostnames are stored verbatim.
 *
 *  Port validation accepts only values in `[1, 65535]`.  Port 0 and ports
 *  that overflow `uint16_t` (e.g. `65536`, `23498765`) all cause the
 *  function to return `false`.
 *
 *  The regex is compiled once as a `static` local and reused across all
 *  calls.  The entire match is wrapped in `catch (...)` because
 *  `boost::regex_match` can throw `std::runtime_error` on pathological
 *  inputs (e.g. a host component consisting of thousands of colons).
 *
 *  @param pUrl  Output struct populated on success; not modified on failure.
 *  @param strUrl  The URI string to parse.
 *  @return `true` if the URI was well-formed and all fields were extracted
 *      successfully; `false` otherwise.
 *  @note `ParsedUrl::operator==` intentionally ignores `username` and
 *      `password` so two URLs pointing at the same endpoint compare equal
 *      regardless of embedded credentials.
 */
bool
parseUrl(ParsedUrl& pUrl, std::string const& strUrl)
{
    static boost::regex const kRE_URL(
        "(?i)\\`\\s*"
        "([[:alpha:]][-+.[:alpha:][:digit:]]*?):"
        // Only `//authority` hier-part URIs are accepted.
        "//"
        "(?:([^:@/]*?)(?::([^@/]*?))?@)?"
        "([[:digit:]:]*[[:digit:]]|\\[[^]]+\\]|[^:/?#]*?)"
        "(?::([[:digit:]]+))?"
        "(/.*)?"
        "\\s*?\\'");
    boost::smatch smMatch;

    try
    {
        if (!boost::regex_match(strUrl, smMatch, kRE_URL))
            return false;
    }
    catch (...)
    {
        return false;
    }

    pUrl.scheme = smMatch[1];
    boost::algorithm::to_lower(pUrl.scheme);
    pUrl.username = smMatch[2];
    pUrl.password = smMatch[3];
    std::string const domain = smMatch[4];
    auto const result = beast::IP::Endpoint::fromStringChecked(domain);
    pUrl.domain = result ? result->address().to_string() : domain;
    std::string const port = smMatch[5];
    if (!port.empty())
    {
        pUrl.port = beast::lexicalCast<std::uint16_t>(port);

        // lexicalCast returns 0 for values outside [1, 65535].
        if (pUrl.port == 0)
        {
            return false;
        }
    }
    pUrl.path = smMatch[6];

    return true;
}

/** Strip leading and trailing whitespace from a string.
 *
 *  The argument is taken by value so the caller receives a new trimmed copy;
 *  passing an rvalue avoids the extra copy entirely.
 *
 *  @param str The string to trim.
 *  @return A copy of `str` with leading and trailing whitespace removed.
 */
std::string
trimWhitespace(std::string str)
{
    boost::trim(str);
    return str;
}

/** Convert a decimal string to a `uint64_t`, returning `nullopt` on failure.
 *
 *  Unlike a sentinel return value such as `0` or `-1`, `std::nullopt`
 *  unambiguously distinguishes parse failure from the valid value `"0"`.
 *  Used primarily in configuration parsing.
 *
 *  @param s Decimal string to convert.
 *  @return The converted value, or `std::nullopt` if `s` is not a valid
 *      decimal representation of a `uint64_t`.
 */
std::optional<std::uint64_t>
toUInt64(std::string const& s)
{
    std::uint64_t result = 0;
    if (beast::lexicalCastChecked(result, s))
        return result;
    return std::nullopt;
}

/** Validate that a domain string is plausibly well-formed for use in an
 *  XRPL validator TOML file.
 *
 *  Two fast-path length checks (4–128 characters) guard the regex from
 *  obviously invalid inputs.  The `static` regex enforces RFC-like hostname
 *  structure: each label must be `[a-zA-Z0-9-]{1,63}` with no leading or
 *  trailing hyphens, and the TLD must be pure alpha with at least two
 *  characters.  The regex is compiled once with
 *  `boost::regex_constants::optimize` and reused across all calls.
 *
 *  @param domain The domain string to validate.
 *  @return `true` if the domain passes structural checks; `false` otherwise.
 *  @note This is not a full RFC 5891 validator.  It rejects some valid
 *      internationalised domain names and does not verify IANA TLD
 *      registration.  Its purpose is to filter obviously malformed inputs
 *      during TOML parsing, not to definitively resolve domains.
 */
bool
isProperlyFormedTomlDomain(std::string_view domain)
{
    if (domain.size() < 4 || domain.size() > 128)
        return false;

    static boost::regex const kRE(
        "^"
        "("
        "(?!-)"
        "[a-zA-Z0-9-]{1,63}"
        "(?<!-)"
        "\\."
        ")+"
        "[A-Za-z]{2,63}"
        "$",
        boost::regex_constants::optimize);

    return boost::regex_match(domain.begin(), domain.end(), kRE);
}

}  // namespace xrpl
