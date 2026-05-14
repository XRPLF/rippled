/** @file
 *  Implements overlay protocol version negotiation for peer connections.
 *
 *  During the HTTP upgrade handshake that opens a peer session, each node
 *  advertises the protocol versions it speaks and the two sides agree on the
 *  highest mutually supported version.  This file owns the entire lifecycle:
 *  the compile-time table of supported versions, the header-string formatter,
 *  the RFC 2616 parser, and the set-intersection negotiation logic.
 *
 *  @see ProtocolVersion.h for the public interface.
 *  @see Handshake.cpp for how the outbound `Upgrade` header is built.
 *  @see ConnectAttempt.cpp for the asymmetric response-side version check.
 */
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/rfc2616.h>

#include <boost/beast/core/string_type.hpp>
#include <boost/iterator/function_output_iterator.hpp>
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

/** Compile-time table of every protocol version this build speaks.
 *
 *  Entries must be in strictly ascending order with no duplicates; the
 *  `static_assert` immediately below enforces this at build time.  The set
 *  is intentionally a compile-time constant — the versions a binary speaks
 *  are a fixed property of that build, not a runtime configuration knob.
 *
 *  @note Add new versions at the end, in ascending order.  A mis-ordered or
 *      duplicate entry produces a build error from the `static_assert` below.
 */
constexpr ProtocolVersion const kSUPPORTED_PROTOCOL_LIST[]{
    {2, 1},
    {2, 2},
};

// Compile-time guard: verifies kSUPPORTED_PROTOCOL_LIST is non-empty and
// strictly ascending (no duplicates).  Uses a hand-rolled loop because
// std::is_sorted is not constexpr before C++20.
// FIXME: simplify with std::is_sorted once the codebase targets C++20.
static_assert(
    []() constexpr -> bool {
        auto const len =
            std::distance(std::begin(kSUPPORTED_PROTOCOL_LIST), std::end(kSUPPORTED_PROTOCOL_LIST));

        // There should be at least one protocol we're willing to speak.
        if (len == 0)
            return false;

        // A list with only one entry is, by definition, sorted so we don't
        // need to check it.
        if (len != 1)
        {
            for (auto i = 0; i != len - 1; ++i)
            {
                if (kSUPPORTED_PROTOCOL_LIST[i] >= kSUPPORTED_PROTOCOL_LIST[i + 1])
                    return false;
            }
        }

        return true;
    }(),
    "The list of supported protocols isn't properly sorted.");

std::string
to_string(ProtocolVersion const& p)
{
    return "XRPL/" + std::to_string(p.first) + "." + std::to_string(p.second);
}

/** Parse and validate protocol version tokens from an RFC 2616 header value.
 *
 *  Splits @p value on commas, then applies three validation layers to each
 *  token before accepting it:
 *  1. **Regex** — enforces the `XRPL/major.minor` lexical form; rejects the
 *     legacy `RTXP/x.y` format and any malformed tokens.
 *  2. **Lexical cast** — converts each captured group to `uint16_t`, rejecting
 *     values that are syntactically valid but would overflow (e.g.
 *     `XRPL/99999.0`).
 *  3. **Round-trip check** — re-serialises the parsed value and compares it
 *     against the original token, catching any phantom discrepancy (e.g.
 *     surviving leading zeroes) that the regex or integer conversion might miss.
 *
 *  The returned vector is sorted in ascending order with duplicates removed,
 *  satisfying the precondition required by `negotiateProtocolVersion`.
 *
 *  @param value  Comma-separated HTTP header value (e.g. from `Upgrade:`).
 *  @return       Validated, sorted, deduplicated list of protocol versions.
 *                Empty if no valid versions were found.
 */
std::vector<ProtocolVersion>
parseProtocolVersions(boost::beast::string_view const& value)
{
    static boost::regex const kRE(
        "^"                        // start of line
        "XRPL/"                    // The string "XRPL/"
        "([2-9]|(?:[1-9][0-9]+))"  // a number (greater than 2 with no leading
                                   // zeroes)
        "\\."                      // a period
        "(0|(?:[1-9][0-9]*))"      // a number (no leading zeroes unless exactly
                                   // zero)
        "$"                        // The end of the string
        ,
        boost::regex_constants::optimize);

    std::vector<ProtocolVersion> result;

    for (auto const& s : beast::rfc2616::splitCommas(value))
    {
        boost::smatch m;

        if (boost::regex_match(s, m, kRE))
        {
            std::uint16_t major = 0;
            std::uint16_t minor = 0;
            if (!beast::lexicalCastChecked(major, std::string(m[1])))
                continue;

            if (!beast::lexicalCastChecked(minor, std::string(m[2])))
                continue;

            auto const proto = makeProtocol(major, minor);

            // Round-trip check: reject tokens whose canonical form differs
            // from the input (e.g. hidden leading zeros that survived casting).
            if (to_string(proto) == s)
                result.push_back(makeProtocol(major, minor));
        }
    }

    std::ranges::sort(result);
    auto const uniq = std::ranges::unique(result);
    result.erase(uniq.begin(), uniq.end());

    return result;
}

/** Select the highest mutually supported protocol version.
 *
 *  Computes the set intersection of @p versions and `kSUPPORTED_PROTOCOL_LIST`.
 *  Both inputs must be sorted in ascending order (guaranteed by the
 *  `static_assert` on the local table and by `parseProtocolVersions` for peer
 *  input).  Because `std::set_intersection` produces output in sorted order,
 *  the lambda output iterator simply overwrites a single `optional` on each
 *  call — the final write is always the greatest intersection element.  This
 *  avoids allocating a temporary container just to read `back()`.
 *
 *  @param versions  Peer's supported versions, sorted ascending, no duplicates.
 *  @return          The highest common version, or `std::nullopt` if there is
 *                   no overlap (caller should close the connection).
 */
std::optional<ProtocolVersion>
negotiateProtocolVersion(std::vector<ProtocolVersion> const& versions)
{
    std::optional<ProtocolVersion> result;

    std::function<void(ProtocolVersion const&)> const pickVersion =
        [&result](ProtocolVersion const& v) { result = v; };

    std::ranges::set_intersection(
        versions, kSUPPORTED_PROTOCOL_LIST, boost::make_function_output_iterator(pickVersion));

    return result;
}

/** Parse @p versions then select the highest mutually supported version.
 *
 *  Convenience overload that calls `parseProtocolVersions` before delegating
 *  to the vector overload.  Use this when working directly with raw HTTP
 *  header values (e.g. the inbound `Upgrade:` header on an HTTP 101 response).
 *
 *  @param versions  Raw comma-separated `Upgrade` header value from a peer.
 *  @return          The highest common version, or `std::nullopt` if there is
 *                   no overlap or @p versions contains no recognisable tokens.
 */
std::optional<ProtocolVersion>
negotiateProtocolVersion(boost::beast::string_view const& versions)
{
    auto const them = parseProtocolVersions(versions);

    return negotiateProtocolVersion(them);
}

/** Return the comma-separated list of supported protocol versions for use in
 *  the HTTP `Upgrade` header.
 *
 *  The string is built exactly once from `kSUPPORTED_PROTOCOL_LIST` via a
 *  local static initialiser and then cached for the lifetime of the process,
 *  avoiding repeated allocation on every outgoing handshake.  The format
 *  matches what `parseProtocolVersions` expects (e.g. `"XRPL/2.1, XRPL/2.2"`).
 *
 *  @return  Reference to a process-lifetime string; never empty.
 */
std::string const&
supportedProtocolVersions()
{
    static std::string const kSUPPORTED = []() {
        std::string ret;
        for (auto const& v : kSUPPORTED_PROTOCOL_LIST)
        {
            if (!ret.empty())
                ret += ", ";
            ret += to_string(v);
        }

        return ret;
    }();

    return kSUPPORTED;
}

/** Return `true` if @p v appears in `kSUPPORTED_PROTOCOL_LIST`.
 *
 *  Uses a linear scan through the small compile-time table.  This is
 *  intentionally different from `negotiateProtocolVersion`: when a peer
 *  echoes back a single selected version in its HTTP 101 response we must
 *  verify membership directly rather than re-running the full negotiation,
 *  because the peer has already chosen — we just need to confirm it picked
 *  something we actually speak.
 *
 *  @param v  Protocol version to look up.
 *  @return   `true` if @p v is locally supported; `false` otherwise.
 */
bool
isProtocolSupported(ProtocolVersion const& v)
{
    return std::end(kSUPPORTED_PROTOCOL_LIST) != std::ranges::find(kSUPPORTED_PROTOCOL_LIST, v);
}

}  // namespace xrpl
