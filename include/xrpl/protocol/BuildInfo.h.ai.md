# `include/xrpl/protocol/BuildInfo.h`

This header declares the `xrpl::BuildInfo` namespace, which owns all versioning concerns for the rippled server binary: human-readable version strings for protocol handshakes and HTTP headers, and a compact 64-bit encoding that lets validators in the network quickly report and compare software versions.

## Why a Dedicated Version Module

XRPL validators embed their software version in consensus validation messages — specifically in the `sfServerVersion` field of `STValidation` objects — so that the network can collectively detect when a majority of validators have upgraded. This requires a version representation that is compact enough to embed in wire-format messages, totally ordered (newer > older via integer comparison), and cross-implementation-aware (a validator running some other xrpl implementation should not be misread as a stale xrpld node). `BuildInfo` satisfies all three requirements.

## String-Based API

`getVersionString()` returns the canonical SemVer string (e.g., `"3.2.0-b0"`). Its implementation is a Meyers singleton: a static local `std::string` initialized once with a lambda that both builds the string and validates it through `beast::SemanticVersion`. If the hard-coded `versionString` constant fails to parse or fails a round-trip through the parser, `LogicError()` is thrown on first call. This is a start-up invariant check: a malformed version constant is caught immediately rather than producing silently wrong encoded integers. In `DEBUG` or sanitizer builds, the commit hash and build-mode metadata are appended as SemVer build metadata (e.g., `"3.2.0-b0+abc1234.DEBUG"`).

`getFullVersionString()` prepends the system name, yielding `"xrpld-3.2.0-b0"`. This form appears verbatim in the `User-Agent` and `Server` HTTP headers during peer-protocol handshakes and in all HTTP responses from the JSON-RPC server — providing a single canonical identifier that third-party tooling can scrape.

## 64-Bit Encoding

`encodeSoftwareVersion(string_view)` packs a SemVer string into a `uint64_t` with the following layout:

```
[15:0]  implementation identifier  (0x183B for xrpld)
[23:16] major version              (8 bits, 0-255)
[31:24] minor version              (8 bits, 0-255)
[39:32] patch version              (8 bits, 0-255)
[41:40] pre-release type           (11 = release, 10 = RC, 01 = beta)
[47:42] pre-release number         (6 bits, 1-63; 0 for releases)
[63:48] reserved zeros
```

The pre-release type encoding is deliberately chosen so that integer ordering matches semantic version ordering: a release (`0b11`) always compares greater than an RC (`0b10`), which compares greater than a beta (`0b01`). This makes `isNewerVersion()` a simple integer comparison, with no need to decode the fields.

The implementation ID `0x183B` occupies the most-significant 16 bits. This lets the network accommodate alternative xrpl implementations: any node broadcasting a version whose top 16 bits are not `0x183B` is treated as an unknown implementation, and `isNewerVersion()` returns `false` for it unconditionally. This is a conservative design — rather than risk misinterpreting a foreign version encoding as older or newer, the node simply declines to make the comparison.

`getEncodedVersion()` caches this node's own encoded value in a static, calling `encodeSoftwareVersion(getVersionString())` exactly once.

## Role in Consensus

At every "voting ledger" (every 256th ledger, when amendments are processed), `RCLConsensus` embeds `getEncodedVersion()` into each validation it broadcasts via the `sfServerVersion` field. `LedgerMaster` then inspects incoming validations: it calls `isXrpldVersion()` and `isNewerVersion()` on each validator's `sfServerVersion` to count how many validators are running a newer xrpld version. This count can be used to surface upgrade warnings — if a significant fraction of the network has already upgraded, an older node has actionable signal to do the same.

## Deprecation Note

The `// VFALCO The namespace is deprecated` comment on the `BuildInfo` namespace suggests a future intent to dissolve this sub-namespace and expose these utilities directly in `xrpl`. The API surface is small enough that such a migration would be purely mechanical.