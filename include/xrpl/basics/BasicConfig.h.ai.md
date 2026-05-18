# `BasicConfig.h` — INI-Style Configuration Substrate

`BasicConfig.h` defines the foundational data model for the XRPL node's configuration system. It sits at the bottom of a two-layer design: this file provides the in-memory representation and query interface for section-based configuration data, while the concrete `Config` class (in `src/xrpld/core/Config.h`) inherits from `BasicConfig` and adds filesystem loading, application-specific typed fields, and validator management. The header comment on `Config` explicitly labels that derived class as deprecated, signaling that `BasicConfig`'s style — decentralized, per-module parsing — is the intended long-term direction.

## Data Model: Two Representations in One `Section`

The `Section` class maintains three parallel containers for the same underlying config content:

- `lookup_` — an `unordered_map<string, string>` for `key = value` pairs, used for named lookups
- `values_` — a `vector<string>` of non-key-value lines (bare tokens like IP addresses or file paths)
- `lines_` — a `vector<string>` containing every non-empty, non-comment line in canonical form

This triple storage isn't redundancy — it reflects the two distinct ways config sections are used in practice. Sections like `[server]` contain key=value pairs consumed by name; sections like `[validators]` contain bare values (one per line) iterated as a list. The `lines()` accessor preserves insertion order, which matters for list-type sections where positional meaning exists.

The `append()` method is where parsing happens. It applies a Boost regex matching `^key=value` to each incoming line. Lines that match go into `lookup_` via `set()`; non-matching lines go into `values_`. Both go into `lines_`. The same method also handles inline comment stripping: `#` characters are treated as comment delimiters unless escaped with `\`. The escape character is consumed when found (`val.erase(comment - 1, 1)`), allowing literal `#` characters in values. This detail is tracked via `had_trailing_comments_`, which bubbles up through `BasicConfig::had_trailing_comments()` via `std::any_of` — presumably to emit a deprecation warning to operators about ambiguous config syntax.

## The "Legacy" Pattern

Some older config sections hold a single freeform value rather than key-value pairs — for example `[node_db]` in its pre-structured form. The `legacy()` getter/setter pair accommodates this by treating the first entry of `lines_` as the canonical value. Reading a `Section` as legacy on a multi-line section intentionally throws `std::runtime_error` via `Throw<>()`, enforcing that this access path is only valid for single-line sections. This prevents silent misreads where code expecting one value silently gets only the first of many.

`BasicConfig` also exposes `legacy()` at the aggregate level, forwarding to the named section's `legacy()`. This provides `config.legacy("section_name")` as a convenience for the many legacy callsites in `Config.cpp`.

## `BasicConfig`: Container and Access Protocol

`BasicConfig` holds an `unordered_map<string, Section>`, keyed by section name. The critical behavioral difference between the const and non-const `section()` overloads reflects a deliberate design choice:

- Non-const `section()` calls `map_.emplace(name, name)` — it auto-creates an empty section on first access. This allows callers to unconditionally call `config["new_section"].set(...)` without precondition checks.
- Const `section()` returns a reference to a `static Section const none("")` sentinel when the section doesn't exist. This avoids exceptions during read-only configuration queries and makes `operator[]` safe to call on a const `BasicConfig` even for absent sections.

The `overwrite()` method is specifically for command-line argument injection, layering CLI-provided values over whatever the config file contains. `deprecatedClearSection()` (name signals intent) wipes a section's content by replacing its `Section` object wholesale — used historically to clear sections before reloading.

The `build()` method is `protected`, not `public`. It consumes an `IniFileSections` (a `unordered_map<string, vector<string>>`), which is the raw pre-parsed form produced by `parseIniFile()` in `Config.cpp`. Subclasses call `build()` after obtaining this intermediate representation, keeping the file I/O and INI parsing out of `BasicConfig` itself.

## Free Function Query Layer

The file exports three sets of free functions designed for module-level configuration consumption:

`set(target, name, section)` reads a named key, casts it via `boost::lexical_cast<T>`, and assigns to `target` only on success — leaving `target` unchanged on missing key or bad cast. The two-argument variant adds an explicit default value applied on failure. Both return `bool` indicating whether the config file actually specified the value, which is important for distinguishing "user set this to the default" from "user didn't set this."

`get(section, name, defaultValue)` is a value-returning variant; it catches `bad_lexical_cast` and falls back to the default silently. An overload handles `char const*` defaults to avoid awkward template deduction with string literals.

`get_if_exists<bool>` is explicitly specialized to read boolean config values as integers (`0` or `1`) rather than as the string tokens `"true"` or `"false"`. This matches the XRPL config file convention where booleans are expressed numerically, and avoids `lexical_cast<bool>` which in Boost accepts `"true"` but not `"1"` depending on locale.

Together these three free functions provide a consistent, exception-safe pattern that modules throughout the codebase use to pull typed values from their respective config sections without having to handle parse failures individually.