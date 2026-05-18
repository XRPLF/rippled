# `SemanticVersion.cpp` — SemVer Parsing, Printing, and Comparison

This file implements `beast::SemanticVersion`, providing a strict, spec-compliant parser and comparator for [Semantic Versioning 2.0](https://semver.org/) strings. Its primary role in the XRPL codebase is version identification — protocol handshakes, feature negotiation, and software build tagging all rely on well-defined version ordering. The implementation deliberately rejects any input that deviates from the specification, including forms that humans might consider "obviously equivalent" like `01.2.3` or `1.2.3 `.

## Parsing Architecture: Destructive Consumption

The parsing strategy is to copy the input into a mutable `std::string`, then consume it left-to-right with a family of "chop" helpers that remove tokens from the front of the string as they recognize them. This approach is simple, side-effect-free from the caller's perspective, and makes the success condition in `parse()` trivially expressible: the string must be fully empty at the end. Any leftover characters mean the input contained something the parser didn't recognize.

`chopUInt()` handles the three integer fields. It scans leading digits, converts them via `lexicalCastChecked`, then enforces two invariants beyond simple conversion: the value must not have leading zeroes (checked by round-tripping through `std::to_string`), and it must fall within a provided upper bound. The round-trip check is the idiomatic way to detect leading zeroes without explicit string inspection — `lexicalCastChecked("01", n)` succeeds with `n=1`, but `std::to_string(1) != "01"` catches the problem.

`chop()` is a simple string-prefix consumer — it returns `true` only if the input begins with the exact literal being chopped, and removes it if so. This is used to consume the `.` separators between version numbers and the `-` / `+` sigils that introduce pre-release and build metadata sections.

`extract_identifier()` reads a single alphanumeric+hyphen token from the input front. The character allowlist is hardcoded as the exact set permitted by SemVer: `[a-zA-Z0-9-]`. A notable design point is `allowLeadingZeroes`: it is `false` for pre-release identifiers (per spec: `0.1` is legal as pre-release but `01` is not) and `true` for build metadata (the spec places no constraint on metadata content beyond character validity). This asymmetry is enforced directly through the `extract_identifiers` call sites in `parse()`.

`parse()` accepts `std::string_view` for zero-copy call sites, but immediately converts to `std::string` for mutable processing. The leading/trailing whitespace check is performed both by scanning to find the trimmed region and by comparing the result back to the input — if they differ, the version string had whitespace and is rejected.

## Two-Level Error Reporting

The design separates the failure mechanism between `parse()` and the constructing overload. `parse()` returns `bool`, allowing callers to probe validity without exceptions. The `SemanticVersion(std::string_view)` constructor calls `parse()` and converts `false` to a `std::invalid_argument` throw. This two-tier design mirrors the pattern used elsewhere in beast utilities — contexts that control their input (e.g., config loading) can call `parse()` and handle failures gracefully, while contexts where an invalid version string is a programming error can use the constructor and let the exception propagate.

## Comparison: Faithful SemVer Precedence

`compare()` returns an integer in the style of `strcmp`, with the full relational operator suite defined inline in the header by delegating to it. The comparison sequence follows the specification exactly:

1. **Major, minor, patch** are compared numerically in order.
2. **Pre-release vs. release**: when the numeric cores are equal, a release version (empty `preReleaseIdentifiers`) always outranks a pre-release. This is the SemVer rule that `1.0.0` > `1.0.0-rc.1`.
3. **Pre-release identifier lists** are compared element by element. Purely numeric identifiers are compared as integers; mixed or all-alpha identifiers are compared lexicographically. When one side's identifier list is exhausted before the other, the longer list wins. `XRPL_ASSERT` macros guard the points where the code assumes both sides are either both numeric or both non-numeric — these fire only if `isNumeric` and the comparison branching above disagree, which would indicate a logic bug.
4. **Build metadata is ignored entirely.** The spec mandates this, and the test suite explicitly verifies it by confirming that `checkLess("1.0.0-alpha", "1.0.0-alpha.1")` holds regardless of whether `+meta` is appended to either or both sides.

## Relationship to Other Files

The header `SemanticVersion.h` declares the `SemanticVersion` struct with public integer fields (`majorVersion`, `minorVersion`, `patchVersion`) and `identifier_list` vectors for pre-release and metadata. The struct has no private state — it is a plain aggregate with a parser. The six relational operators are all inline in the header, each a one-liner forwarding to `compare()`. This means callers only need to link one function symbol while getting a full ordered-comparison interface.

`LexicalCast.h` provides `lexicalCastChecked` (try-cast returning bool) and `lexicalCastThrow` (cast or throw), used for integer parsing in `chopUInt` and identifier numeric comparison in `compare()` respectively. Using `lexicalCastThrow` in `compare()` is safe because the numeric classification was already established by `isNumeric()` before the cast attempt, and `XRPL_ASSERT` guards the logical consistency of that classification.

The test file `SemanticVersion_test.cpp` exercises parsing at multiple levels of composition: individual pass/fail checks, decomposition verification that parsed fields match expected values, and comparison tests against the canonical precedence chain from the SemVer spec (`1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < ... < 1.0.0`). The test also verifies the metadata-independence of all comparison results by re-running each comparison with `+meta` suffixes appended.