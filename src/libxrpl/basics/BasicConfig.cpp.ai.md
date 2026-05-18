# `BasicConfig.cpp` — INI Section Parsing and Configuration Storage

This file implements the two core classes of the XRPL configuration subsystem: `Section` and `BasicConfig`. Together they handle the mid-layer of config processing — between raw INI file text (tokenized by `parseIniFile()` in `Config.cpp`) and the typed, module-specific configuration objects that consume it. The file's job is to parse lines into structured key-value maps while preserving ordering and handling comment syntax.

## Two-Tier Configuration Model

`IniFileSections` (defined as `std::unordered_map<std::string, std::vector<std::string>>`) is the bridge type produced by `parseIniFile()`. It maps section names to their raw, unprocessed text lines. `BasicConfig::build()` consumes this structure by iterating entries and calling `Section::append()` on each, converting opaque line vectors into queryable `Section` objects stored in `map_`. This design keeps file I/O and INI tokenization entirely outside `BasicConfig` — the class only sees already-tokenized line vectors.

`build()` is declared `protected`, not `public`. This is a deliberate architectural boundary: only `Config` (which subclasses `BasicConfig`) can call it during the load sequence. External callers interact only through the query interface and mutation methods like `overwrite()`.

## `Section::append()` — The Core Parser

The central logic lives in `Section::append()`. For each incoming line it performs two passes:

First, an inline `remove_comment` lambda scans for `#` characters. If a `#` is preceded by `\`, the backslash is erased and scanning resumes from the same position — enabling escaped comment characters in values. A bare `#` truncates the line at that point via `trim_whitespace`, and the `had_trailing_comments_` flag is set to record that truncation occurred. A leading `#` (entire line is a comment) zeroes the string immediately. This is a manual scan rather than regex because the escape-handling and mutation-in-place logic requires iterative state.

Second, the cleaned line is matched against a static compiled regex:

```
^(?:\s*)([a-zA-Z][_a-zA-Z0-9]*)(?:\s*)(?:=)(?:\s*)(.*\S+)(?:\s*)
```

The regex enforces that keys start with a letter and contain only alphanumerics and underscores, and that values contain at least one non-whitespace character. The `boost::regex_constants::optimize` flag is passed, and the regex object is `static const` — compiled exactly once across the process lifetime.

Lines that fail the regex are **not discarded** — they are pushed to `values_`. This is intentional: many config sections contain positional entries that are not key-value pairs (peer IPs, validator keys, file paths). The `values_` vector captures these for module-specific downstream parsing, while `lines_` captures everything (including lines that matched as key-value pairs) to maintain the full input record.

## Three Parallel Storage Vectors

`Section` maintains three storage structures simultaneously:

- `lookup_` (`std::unordered_map<string, string>`): key-value pairs for named setting retrieval via `get()` and `value_or()`
- `lines_`: every processed line, after comment stripping, in order — the complete record
- `values_`: only non-key-value lines — bare positional data

This tripartite design avoids forcing all config into the key-value paradigm while still providing O(1) lookups for named settings. The `lookup_` map uses `insert_or_assign` in `set()`, so repeated calls (e.g., from `append()` followed by `overwrite()`) always take the last value without error.

## The Null-Object Pattern for Missing Sections

The `const` overload of `BasicConfig::section()` returns a reference to a `static Section const none("")` when the requested name isn't found, rather than throwing or returning a pointer:

```cpp
Section const&
BasicConfig::section(std::string const& name) const
{
    static Section const none("");
    auto const iter = map_.find(name);
    if (iter == map_.end())
        return none;
    return iter->second;
}
```

This enables the common call pattern `config["missing_section"].get<int>("key")` to safely return `std::nullopt` without the caller needing null checks. The mutable overload, by contrast, uses `map_.emplace(name, name)` which auto-creates the section on demand — appropriate for mutation paths like `overwrite()` but not for const queries.

## Mutation After Load: `overwrite()` and `deprecatedClearSection()`

`overwrite()` creates the target section via `std::piecewise_construct` if it doesn't exist, then calls `Section::set()` directly — bypassing `append()` and its comment/regex machinery. This is the path used for command-line argument injection, where CLI-supplied values must take precedence over file-based config regardless of format concerns.

`deprecatedClearSection()` replaces an existing `Section` object wholesale with a fresh empty one, erasing all key-value pairs and lines. The `deprecated` prefix in the name is an explicit signal that this operation pattern is considered a design smell and its callers are candidates for refactoring.

## Legacy Mode

Some older XRPL config sections hold a single bare value (not key=value pairs) — for example, a database path or a simple flag. The `legacy()` pair of methods handles this: `BasicConfig::legacy(section, value)` injects a raw string into a section's first `lines_` slot via `Section::legacy(string)`, and the getter enforces the invariant that `legacy()` is only valid on sections with exactly one line (throwing `std::runtime_error` otherwise). This is backward compatibility scaffolding, not a general pattern.

## `had_trailing_comments_` and Round-Trip Fidelity

The `had_trailing_comments_` flag on `Section`, aggregated at the `BasicConfig` level by `had_trailing_comments()`, indicates whether any loaded section had inline comments that were stripped from values. This is surfaced so that the application can warn users that round-tripping the config (rewriting it from the in-memory representation) would lose those comments — the `operator<<` overloads only emit `key=value` pairs and section headers, not comments.