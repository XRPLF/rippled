# XRPLD Documentation Standards

This document defines the canonical format for inline code documentation in the
xrpld codebase. All new and updated code must follow these standards.

## Comment Style

Use Javadoc-style Doxygen comments (`/** ... */`). This matches the dominant
convention in the codebase: ~5,200 existing instances across 569 files.

```cpp
/** Brief description of the entity. */
```

For multi-line documentation, each line is prefixed with ` * ` (space, asterisk,
space):

```cpp
/** Brief description of the entity.
 *
 *  Extended description with behavioral details, invariants,
 *  and constraints that are not obvious from the signature.
 */
```

`JAVADOC_AUTOBRIEF = YES` is enabled in the Doxyfile, so the first sentence
of any `/** */` block is automatically treated as the brief. An explicit
`@brief` tag is accepted but not required.

The `///` triple-slash style appears in ~37 files (340 instances). It is
valid Doxygen and will not be removed where it exists, but new code should
use `/** */` for consistency with the majority style.

## What to Document

### File-Level (Optional)

The `@file` tag is not currently used anywhere in the codebase. Adding
file-level documentation is encouraged for complex modules where a
high-level overview helps, but it is not required:

```cpp
/** @file
 *  Defines the Payment transactor for the XRP Ledger.
 *
 *  The Payment transactor handles direct XRP transfers, cross-currency
 *  payments via the pathfinding engine, and partial payments.
 */
```

Module-level READMEs (e.g., `src/xrpld/peerfinder/README.md`) remain the
primary place for architectural documentation.

### Class / Struct Level

Every class and struct gets a doc block describing:
- What it does (1-2 sentences)
- Key invariants or constraints, if any
- Thread-safety guarantees, if relevant
- Lifecycle notes, if relevant

```cpp
/** Executes a Payment transaction on the XRP Ledger.
 *
 *  Supports direct XRP payments, cross-currency payments via RippleCalc,
 *  and partial payments (tfPartialPayment). Path count is limited to 6
 *  with max path length of 8.
 */
class Payment : public Transactor { ... };
```

Target: 2-5 lines for most classes. Complex classes may need more.

### Public Methods and Free Functions

Every public method and free function in headers gets:
- Brief description of behavior (not a restatement of the signature)
- `@param` for each parameter
- `@return` describing what is returned
- `@throw` if it can throw (either `@throw` or `@throws` is acceptable —
  the codebase uses both)
- `@note` for non-obvious constraints or edge cases

When a `@param` description wraps, continuation lines are indented with
4 spaces from the `*`:

```cpp
/** Round a Number value to the precision of a given asset.
 *
 *  For IOUs, rounds to the IOU's scale. For XRP and MPT, no rounding
 *  is performed.
 *
 *  @param asset The relevant asset
 *  @param value The value to be rounded
 *  @param scale An exponent value to establish the precision limit of
 *      `value`. Should be larger than `value.exponent()`.
 *  @return The rounded Number.
 */
[[nodiscard]] Number
roundToAsset(Asset const& asset, Number const& value, int scale);
```

Target: 1-3 lines of description plus `@param`/`@return`.

### Private Methods

Document private methods only when the logic is non-obvious. A brief
one-line comment is sufficient.

### Enums and Constants

All enums get a brief class-level description. Individual enum values get
inline documentation when the meaning is not self-evident:

```cpp
/** Result codes for transaction processing. */
enum TERCode
{
    tesSUCCESS = 0,          /**< Transaction succeeded. */
    tecCLAIM = 100,          /**< Fee claimed; transaction failed. */
    tecPATH_PARTIAL = 101,   /**< Path could not deliver full amount. */
};
```

## What NOT to Document

- Do not paraphrase the function signature. `/** Returns the account ID. */`
  on `AccountID getAccountID()` adds zero information.
- Do not document what is obvious from well-named identifiers.
- Do not reference specific issues, PRs, or task numbers. These belong in
  commit messages and rot as the codebase evolves.
- Do not add multi-paragraph docstrings. If it takes that long to explain,
  the code may need restructuring.
- Do not document `.cpp` implementation files exhaustively. Focus docs on
  headers where the public interface is defined.

## Quality Over Quantity

Wrong documentation is worse than no documentation. Every doc comment must
accurately describe the current behavior. When in doubt:

- Read the implementation before writing the doc
- Cross-reference against test files for edge cases
- Use `@note` to flag subtle behavior that has caught contributors before

## Doxygen Tags Reference

Tags in regular use across the codebase:

| Tag | Codebase Usage | Purpose |
|-----|----------------|---------|
| `@brief` | ~2,500 instances | Brief description (optional — autobrief is enabled) |
| `@param` | ~2,400 instances | Function parameter description |
| `@return` | ~2,200 instances | Return value description |
| `@note` | ~270 instances | Important behavioral note or caveat |
| `@throw` / `@throws` | ~450 instances combined | Exception specification |
| `@see` | ~64 instances | Cross-reference to related entities |
| `@tparam` | ~43 instances | Template parameter description |

Tags used rarely but accepted:

| Tag | Codebase Usage | Purpose |
|-----|----------------|---------|
| `@invariant` | ~13 instances | Property that must always hold |
| `@pre` | ~3 instances | Precondition |
| `@file` | 0 instances | File-level description (new convention, optional) |

## Enforcement

Documentation coverage is measured by [coverxygen](https://github.com/psycofdj/coverxygen)
and enforced in CI:

- PRs cannot decrease documentation coverage (`no_decrease` ratchet mode)
- New files added in a PR require >= 80% doc coverage
- Module-specific thresholds increase quarterly
- The doc-review GitHub Action checks whether code changes invalidate
  existing documentation

Coverage is measured against public API surface: classes, structs,
functions, enums, typedefs, and variables. Private implementation details
are not counted.

## Style Notes

- Doc comments go immediately before the entity they describe (no blank
  line between the comment and the declaration)
- Keep `@param` descriptions on a single line when possible
- For wrapped `@param` descriptions, indent continuation lines 4 spaces
  from the `*`
- Use `@see` sparingly — only when the relationship is non-obvious
- Code style (braces, line width, formatting) is governed by `.clang-format`
  and is independent of these documentation standards
