/** @file
 *  Type system and API for XRPL peer-to-peer protocol version negotiation.
 *
 *  During the HTTP upgrade handshake that opens a peer session, each node
 *  advertises the protocol versions it speaks (via `supportedProtocolVersions`)
 *  and the two sides agree on the highest mutually supported version (via
 *  `negotiateProtocolVersion`).  The result governs message framing and feature
 *  availability for the entire lifetime of that peer session.
 *
 *  @see Handshake.cpp for outbound `Upgrade` header construction.
 *  @see OverlayImpl.cpp for inbound header parsing and negotiation.
 */

#pragma once

#include <boost/beast/core/string.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** A major/minor version number for the XRPL peer-to-peer wire protocol.
 *
 *  Encoded as `std::pair<major, minor>` so that `std::pair`'s built-in
 *  lexicographic ordering gives correct version comparison and sorting for
 *  free — a prerequisite for the set-intersection logic in
 *  `negotiateProtocolVersion`.  The canonical string form is
 *  `"XRPL/major.minor"` (e.g. `"XRPL/2.1"`), used directly as the value of
 *  the HTTP `Upgrade` header during handshake.
 */
using ProtocolVersion = std::pair<std::uint16_t, std::uint16_t>;

/** Construct a `ProtocolVersion` from explicit major and minor numbers.
 *
 *  Convenience factory that names the construction intent at call sites
 *  instead of requiring callers to write raw brace-initialization.
 *
 *  @param major  Major version component.
 *  @param minor  Minor version component.
 *  @return       The corresponding `ProtocolVersion`.
 */
constexpr ProtocolVersion
makeProtocol(std::uint16_t major, std::uint16_t minor)
{
    return {major, minor};
}

/** Format a protocol version as the canonical `"XRPL/major.minor"` string.
 *
 *  The returned string is used directly as the value of the HTTP `Upgrade`
 *  header (e.g. `"XRPL/2.1"`).
 *
 *  @param p  The protocol version to format.
 *  @return   Human-readable string in the form `"XRPL/major.minor"`.
 */
std::string
to_string(ProtocolVersion const& p);

/** Parse valid XRPL protocol version tokens from a comma-separated header value.
 *
 *  Accepts the raw value of an HTTP `Upgrade` header (e.g.
 *  `"RTXP/1.2, XRPL/2.1, XRPL/2.2"`) and extracts only those tokens that
 *  represent recognized XRPL protocol versions.  Three validation layers are
 *  applied to each token: a regex that enforces the `XRPL/major.minor` lexical
 *  form and rejects the legacy `RTXP` protocol; a numeric range check that
 *  rejects values overflowing `uint16_t`; and a round-trip check that
 *  re-serializes the parsed version and compares it to the original token to
 *  catch any residual edge cases (e.g. surviving leading zeros).
 *
 *  @param s  Comma-separated HTTP header value to parse.
 *  @return   Validated protocol versions in ascending order with duplicates
 *      removed.  Empty if no valid tokens were found.
 *
 *  @note The sorted, deduplicated guarantee is a precondition required by
 *      the vector overload of `negotiateProtocolVersion`.
 */
std::vector<ProtocolVersion>
parseProtocolVersions(boost::beast::string_view const& s);

/** Select the highest mutually supported protocol version from a parsed list.
 *
 *  Computes the set intersection of @p versions and the compile-time table of
 *  versions this build supports, then returns the highest element of that
 *  intersection.  Choosing the highest common version maximizes available
 *  features while maintaining backward compatibility with older peers.
 *
 *  @param versions  Peer's supported versions, sorted ascending, no duplicates
 *      (as produced by `parseProtocolVersions`).
 *  @return  The highest mutually supported version, or `std::nullopt` if there
 *      is no overlap — the caller should reject the connection in that case.
 */
std::optional<ProtocolVersion>
negotiateProtocolVersion(std::vector<ProtocolVersion> const& versions);

/** Parse a raw header value and select the highest mutually supported version.
 *
 *  Convenience overload for callers holding a raw HTTP `Upgrade` header value.
 *  Delegates to `parseProtocolVersions` and then the vector overload of
 *  `negotiateProtocolVersion`.
 *
 *  @param versions  Raw comma-separated `Upgrade` header value from a peer.
 *  @return  The highest mutually supported version, or `std::nullopt` if there
 *      is no overlap or @p versions contains no recognizable XRPL tokens.
 */
std::optional<ProtocolVersion>
negotiateProtocolVersion(boost::beast::string_view const& versions);

/** Return the comma-separated `Upgrade` header value advertising all locally
 *  supported protocol versions (e.g. `"XRPL/2.1, XRPL/2.2"`).
 *
 *  The string is computed once from the compile-time version table and cached
 *  in a function-local static for the lifetime of the process, avoiding
 *  repeated allocation on every outgoing handshake.  Used by `Handshake.cpp`
 *  when constructing the outbound HTTP `GET` request.
 *
 *  @return  Reference to a process-lifetime string; never empty.
 */
std::string const&
supportedProtocolVersions();

/** Check whether a specific protocol version is locally supported.
 *
 *  Used when a peer echoes back a single selected version in its HTTP 101
 *  response and the receiver needs to confirm membership directly, rather than
 *  re-running full negotiation.
 *
 *  @param v  Protocol version to look up.
 *  @return   `true` if @p v appears in the compile-time supported-version
 *      table; `false` otherwise.
 */
bool
isProtocolSupported(ProtocolVersion const& v);

}  // namespace xrpl
