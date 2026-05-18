# `include/xrpl/beast/core/SemanticVersion.h`

## Role and Purpose

`SemanticVersion.h` provides a strict, spec-compliant implementation of [Semantic Versioning 2.0.0](http://semver.org/) within the `beast` namespace. Its primary job is to parse, store, serialize, and compare version strings in the form `MAJOR.MINOR.PATCH[-pre-release][+metadata]`. The class exists because the XRPL node (`rippled`) publishes its own software version both at startup and over the peer-to-peer network, requiring a reliable and unambiguous way to parse, validate, and compare those version strings.

## The `SemanticVersion` Class

The class is a straightforward data-holder: three `int` fields (`majorVersion`, `minorVersion`, `patchVersion`) and two `identifier_list` members (both `std::vector<std::string>`) for `preReleaseIdentifiers` and `metaData`. This flat, public-member design reflects the fact that `SemanticVersion` is not encapsulating behavior on those fields — it is a structured representation that other code reads directly. The two constructors offer different contracts: the default constructor zero-initializes, while the `string_view` constructor throws `std::invalid_argument` if parsing fails. This split lets callers choose between exception-based control flow (when a valid version is expected) and error-code control flow via `parse()` (when handling untrusted input).

## Parsing is Deliberately Strict

`parse()` rejects anything the semver specification forbids: leading or trailing whitespace, leading zeroes on any numeric component (e.g., `01.2.3`), negative integers, missing components, and stray characters after the version string. The implementation in `SemanticVersion.cpp` is a character-consuming parser — `chop()` removes an exact prefix, `chopUInt()` reads and removes a decimal integer while enforcing no-leading-zero and range constraints, and `extract_identifier()`/`extract_identifiers()` handle the dot-delimited pre-release and metadata fields. Metadata identifiers are allowed to have leading zeroes (the semver spec permits this for the `+metadata` section, though not for pre-release identifiers), so `extract_identifiers` takes an `allowLeadingZeroes` flag that is set differently for each section. The function returns `false` rather than throwing so callers can treat invalid versions as a recoverable condition.

## Comparison Rules and the `compare()` Function

The free function `compare(lhs, rhs)` implements the semver precedence order and returns a three-way result (`-1`, `0`, `1`) in the style of `strcmp`, so all six comparison operators can be implemented as inline forwarding wrappers. This design avoids duplicating the comparison logic and makes the operators zero-overhead.

The comparison logic follows the spec faithfully:

1. **Major, minor, patch** are compared numerically in order. Any difference here is decisive.
2. **Pre-release vs. release**: a pre-release version (`1.0.0-alpha`) is always less than the corresponding release (`1.0.0`) even though their numeric components are identical. This is handled by checking `isRelease()` (which simply returns `preReleaseIdentifiers.empty()`) before entering field-by-field pre-release comparison.
3. **Pre-release identifier ordering**: identifiers are compared pairwise. Numeric identifiers are cast back to `int` and compared by value (so `beta.9 < beta.11`), while alphanumeric identifiers use lexicographic string comparison. Purely numeric identifiers sort below alphanumeric ones of the same position. When one version's pre-release list is exhausted before the other's, the longer list wins (e.g., `1.0.0-alpha < 1.0.0-alpha.1`).
4. **Build metadata is ignored entirely** — two versions that differ only in `+metadata` compare as equal. This is a deliberate semver spec rule: metadata is for informational context (e.g., build flags, git hashes) and must not affect version ordering.

## Usage in the XRPL Network Stack

`SemanticVersion` has two concrete consumers in the codebase:

**`BuildInfo.cpp`** uses it in two ways. First, the version string constant (currently `"3.2.0-b0"`) is validated at startup inside a static initializer: `v.parse(s)` must succeed and `v.print() == s` must hold, guaranteeing that the hard-coded version string is well-formed and round-trips cleanly. A failure calls `LogicError`, crashing the process — this is intentional: a malformed version string is a build-time mistake, not a runtime recoverable condition. Second, `encodeSoftwareVersion()` parses the version string to pack `major`, `minor`, `patch`, and pre-release type (`rc` or `b`) into a compact 64-bit integer for peer-to-peer version advertisement, using `isPreRelease()` and iterating `preReleaseIdentifiers` to classify the build stage.

**`ApiVersion.h`** uses it for legacy API version 1 responses. When the API version is unspecified (defaulting to version 1), the server's `version` response object includes `first`, `good`, and `last` fields represented as semver strings (`"1.0.0"`), instantiated as `static beast::SemanticVersion` objects.

## Invariants and Defensive Patterns

The `compare()` function uses `XRPL_ASSERT` to enforce that when one identifier is numeric, the other must be as well (and vice versa), since the identifier-type classification is done before branching. The test suite (`SemanticVersion_test.cpp`) exhaustively verifies the canonical semver ordering example from the specification — `1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0` — and also validates that metadata suffixes do not affect any comparison outcome, confirming correct spec compliance.