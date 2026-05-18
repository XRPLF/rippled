# `BuildInfo.cpp` — Version Identity and Network Encoding for xrpld

This file owns everything the XRPL daemon uses to identify itself: the human-readable release string, its derivation into a compact 64-bit wire encoding, and the comparison utilities that let validators detect when peers are running newer software.

## The Version String Pipeline

The raw version constant `versionString` (e.g., `"3.2.0-b0"`) is a `const char*` in an anonymous namespace — the single edit point a developer touches when cutting a release. From there it passes through two transforms before becoming public.

`buildVersionString()` (private, never exported) optionally appends SemVer build metadata. In `DEBUG` or sanitizer builds, it queries `xrpl::git::getCommitHash()` to prepend the commit, then tacks on `DEBUG` and/or the stringified `SANITIZERS` macro. The result looks like `3.2.0-b0+a1b2c3d.DEBUG.address`. The `BOOST_PP_STRINGIZE` call is the only way to convert a preprocessor-defined token list (`-DSANITIZERS=address,undefined`) into a runtime string.

`getVersionString()` is the public entry point and uses a `static const` local variable — C++11-guaranteed to initialize exactly once and thread-safely — to memoize the result. Before caching, it round-trips through `beast::SemanticVersion`: it parses the string, then checks `v.print() == s`. This double check catches both malformed strings (parse fails) and strings that are semantically valid but not in canonical form (parse succeeds but the canonical form differs). A failure calls `LogicError`, which throws unconditionally. Since `getVersionString()` is called during application startup, any version string typo kills the process immediately rather than silently propagating a bad identity.

`getFullVersionString()` prepends the system name from `systemName()` (which returns `"xrpld"`) to produce identifiers like `xrpld-3.2.0-b0`. This composite form appears in the `User-Agent` header of HTTP requests made by the node and in startup log messages.

## The 64-bit Wire Encoding

The XRP Ledger peer protocol needs to include version information in consensus validation messages without the cost of string comparison. `encodeSoftwareVersion()` compresses an entire semantic version string into a `uint64_t` with a fixed layout described in the header:

```
0x183B | MAJOR(8) | MINOR(8) | PATCH(8) | TYPE(2) | NUMBER(6) | 0x0000
```

The upper 16 bits `0x183B` act as an implementation fingerprint — a namespace that distinguishes xrpld versions from any other software that also publishes version numbers on the XRPL network. This makes integer comparison safe: without it, an alien implementation with a high version number could appear "newer" in a purely numeric comparison.

The `TYPE` field (bits 23–22 within the lower 48) uses a deliberate encoding: `0b11` for releases, `0b10` for release candidates, `0b01` for betas. This ordering means a plain integer comparison on the entire `uint64_t` gives correct semantic ordering — a release is numerically greater than an RC of the same version, which is greater than a beta — without any special-case logic at comparison time.

The `parsePreRelease` lambda handles extraction of the pre-release type and number. It checks for a `"rc"` or `"b"` prefix, then uses `beast::lexicalCastChecked` for safe string-to-integer conversion, and `std::clamp` to enforce the 0–63 range. On any failure — empty suffix, non-numeric suffix, out-of-range number — it returns zero silently. The pre-release byte in the encoded version then remains zero, which is correct: unknown or malformed pre-release identifiers sort below any known type.

Both `getEncodedVersion()` (this node's own encoded version) and the result of `encodeSoftwareVersion()` for known versions are lazily computed and cached as statics.

## Network Integration

During consensus, every flag ledger (every 256 ledgers), the local node writes `getEncodedVersion()` into the `sfServerVersion` field of its validation message. `LedgerMaster` collects these values from all validators and calls `isXrpldVersion()` and `isNewerVersion()` to tally how many peers are running xrpld and how many are running a newer release. This drives version upgrade notifications and network health diagnostics.

`isNewerVersion()` explicitly guards against non-xrpld versions by calling `isXrpldVersion()` first and returning `false` for any version with an unrecognized upper-16 fingerprint. This is the critical safety valve: a non-xrpld peer advertising a very large `uint64_t` cannot trick the local node into believing it is running outdated software.