/** @file
 *  Version identity and wire-encoding utilities for the xrpld binary.
 *
 *  Owns the canonical SemVer version string, the composite `systemName-version`
 *  identifier used in HTTP headers and peer-protocol handshakes, and a compact
 *  64-bit encoding that lets validators compare software versions during
 *  consensus via a plain integer comparison — no string parsing required at
 *  runtime.
 *
 *  At every flag ledger (every 256 ledgers) `RCLConsensus` writes the result of
 *  `getEncodedVersion()` into `sfServerVersion` in each validation message it
 *  broadcasts. `LedgerMaster` then inspects incoming validations, calling
 *  `isXrpldVersion()` and `isNewerVersion()` to count how many UNL validators
 *  are running a newer build, which can surface an upgrade notification.
 */

#pragma once

#include <cstdint>
#include <string>

/** Version identity, wire encoding, and peer-comparison utilities for xrpld.
 *
 *  @deprecated The `BuildInfo` sub-namespace is expected to be dissolved; these
 *      utilities will eventually be promoted directly into `xrpl`.
 */
// VFALCO The namespace is deprecated
namespace xrpl::BuildInfo {

/** Return the canonical SemVer version string for this build.
 *
 *  The result is memoized; the initializer runs exactly once (C++11
 *  thread-safe static-init guarantee). On first call the hard-coded
 *  `versionString` constant is round-tripped through `beast::SemanticVersion`:
 *  if it fails to parse, or if its canonical re-serialization differs from the
 *  original, `LogicError` is thrown and the process terminates. This acts as a
 *  start-up invariant check — a malformed version constant is caught
 *  immediately rather than producing silently wrong encoded integers.
 *
 *  In `DEBUG` or sanitizer builds, SemVer build metadata (commit hash,
 *  `DEBUG`, sanitizer names) is appended as a `+`-separated suffix, e.g.
 *  `"3.2.0-b0+abc1234.DEBUG"`.
 *
 *  @return a reference to the cached, validated version string (e.g.
 *      `"3.2.0-b0"`).
 *  @throw LogicError on first call if `versionString` is malformed or
 *      not in canonical SemVer form.
 */
std::string const&
getVersionString();

/** Return the composite `systemName-version` string for this build.
 *
 *  Prepends `systemName()` (always `"xrpld"`) to `getVersionString()`,
 *  separated by `"-"`, e.g. `"xrpld-3.2.0-b0"`. This string appears verbatim
 *  in the `User-Agent` and `Server` HTTP headers during peer-protocol
 *  handshakes and in all HTTP responses from the JSON-RPC server.
 *
 *  @return a reference to the cached composite version string.
 */
std::string const&
getFullVersionString();

/** Encode a SemVer string into the 64-bit wire format used in `sfServerVersion`.
 *
 *  The resulting integer has the following bit layout:
 *
 *  ```
 *  [63:48]  implementation identifier  (0x183B for xrpld)
 *  [47:40]  major version              (8 bits, 0-255)
 *  [39:32]  minor version              (8 bits, 0-255)
 *  [31:24]  patch version              (8 bits, 0-255)
 *  [23:22]  pre-release type           (0b11 = release, 0b10 = RC, 0b01 = beta)
 *  [21:16]  pre-release number         (6 bits, 0-63; 0 for releases)
 *  [15:0]   reserved zeros
 *  ```
 *
 *  The pre-release type bits are deliberately ordered so that a plain integer
 *  comparison on the full `uint64_t` yields correct semantic ordering:
 *  release (`0b11`) > RC (`0b10`) > beta (`0b01`). A malformed pre-release
 *  identifier (missing number, non-numeric suffix, number out of range
 *  [0, 63]) silently yields zero for bits [23:16], which sorts below any
 *  recognizable pre-release type. If `versionStr` does not parse as valid
 *  SemVer at all, the return value contains only the xrpld fingerprint
 *  (`0x183B`) in bits [63:48] and zeros elsewhere.
 *
 *  @param versionStr a SemVer-formatted version string (e.g. `"3.2.0-b0"`).
 *  @return the packed version as a `uint64_t`; see bit layout above.
 */
std::uint64_t
encodeSoftwareVersion(std::string_view versionStr);

/** Return this node's own encoded version, cached from `getVersionString()`.
 *
 *  Calls `encodeSoftwareVersion(getVersionString())` exactly once and caches
 *  the result as a function-local static. This value is written into
 *  `sfServerVersion` in every validation message emitted on flag ledgers by
 *  `RCLConsensus`.
 *
 *  @return the cached 64-bit encoded version for this build.
 */
std::uint64_t
getEncodedVersion();

/** Return true if `version` carries the xrpld implementation fingerprint.
 *
 *  Checks only the upper 16 bits against the xrpld identifier `0x183B`. This
 *  must be called before any numeric comparison of version values: a non-xrpld
 *  peer could advertise an arbitrarily large integer that would otherwise
 *  appear "newer", so `isNewerVersion()` calls this guard unconditionally.
 *
 *  @param version an encoded software version read from `sfServerVersion`.
 *  @return true iff the upper 16 bits of `version` equal `0x183B`.
 */
bool
isXrpldVersion(std::uint64_t version);

/** Return true if `version` represents a strictly newer xrpld release than
 *  this node.
 *
 *  Guards against non-xrpld peers by calling `isXrpldVersion()` first: any
 *  value whose upper 16 bits differ from `0x183B` unconditionally returns
 *  false, regardless of its numeric magnitude. For confirmed xrpld versions,
 *  a plain integer comparison is sufficient because `encodeSoftwareVersion()`
 *  places major, minor, patch, and release-type bits in descending order of
 *  significance.
 *
 *  @param version an encoded software version read from `sfServerVersion`.
 *  @return true iff `version` is an xrpld version strictly greater than
 *      `getEncodedVersion()`.
 *  @see isXrpldVersion(), encodeSoftwareVersion()
 */
bool
isNewerVersion(std::uint64_t version);

}  // namespace xrpl::BuildInfo
