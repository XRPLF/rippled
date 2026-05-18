# `ConfigSections.h` — Configuration Section Name Registry

This header serves a single, focused purpose: it is the canonical registry of every named section that the XRPL node configuration system (`rippled.cfg`) recognizes. Rather than scattering raw string literals across the codebase, all callers include this file and reference the constant by name, so a section rename requires only one edit.

## Two-Tier Design (and Why One Tier Is Deprecated)

The file exposes two parallel mechanisms that reflect different eras of the configuration system's evolution.

**`ConfigSection` struct** (lines 8–22) is the older mechanism, kept alive under a `// VFALCO DEPRECATED` banner. It provides two static `std::string`-returning methods, `nodeDatabase()` → `"node_db"` and `importNodeDatabase()` → `"import_db"`, which name the on-disk key-value store sections. These were wrapped in a struct presumably to namespace them and avoid bare string literals, but the struct itself adds no state — the `explicit ConfigSection() = default` constructor is never meant to be called. The callers that remain (primarily test utilities such as `envconfig.cpp` and `SHAMapStore_test.cpp`) use expressions like `cfg->section(ConfigSection::nodeDatabase())` and `cfg->overwrite(ConfigSection::nodeDatabase(), ...)` to manipulate the in-memory `BasicConfig` section map. These call sites predate the newer interface but have not been migrated yet.

**`#define SECTION_*` macros** (lines 25–78) cover the full range of top-level `[section]` blocks a `rippled.cfg` file may contain. They are plain C-string literals used directly in `Config::load()` (via `detail/Config.cpp`) with helpers like `getSingleSection()`, `getIniFileSection()`, and `exists()`. Because they are raw string literals rather than `std::string` variables, they can be freely concatenated at compile time inside error messages, e.g.:

```cpp
Throw<std::runtime_error>("Cannot have both [" SECTION_VALIDATION_SEED
                          "] and [" SECTION_VALIDATOR_TOKEN "] config sections");
```

The `// VFALCO TODO` annotation at line 24 acknowledges that macros are the wrong tool and that these should become typed constants (e.g., `constexpr std::string_view`), but the migration has not happened.

## Scope of Coverage

The 54 macros span every major subsystem: peer networking (`SECTION_IPS`, `SECTION_IPS_FIXED`, `SECTION_PEERS_MAX`, `SECTION_OVERLAY`), validator infrastructure (`SECTION_VALIDATORS`, `SECTION_VALIDATOR_TOKEN`, `SECTION_VALIDATOR_LIST_SITES`, `SECTION_VALIDATOR_LIST_THRESHOLD`), amendment governance (`SECTION_AMENDMENTS`, `SECTION_VETO_AMENDMENTS`, `SECTION_AMENDMENT_MAJORITY_TIME`), threading (`SECTION_WORKERS`, `SECTION_IO_WORKERS`, `SECTION_PREFETCH_WORKERS`), SSL (`SECTION_SSL_VERIFY`, `SECTION_SSL_VERIFY_FILE`, `SECTION_SSL_VERIFY_DIR`), path-finding (`SECTION_PATH_SEARCH*`), reduce-relay (`SECTION_REDUCE_RELAY`), and several operational toggles. Each macro's string value matches exactly what a node operator types as a bracketed heading in `rippled.cfg`.

## Relationship to `Config.h` and `BasicConfig`

`Config` (in `Config.h`) derives from `BasicConfig`, which owns a `std::map<std::string, Section>` keyed on these exact string values. `ConfigSections.h` is therefore the bridge between the human-readable config file format and the typed C++ `Config` object — every `getSingleSection()` or `section()` call in `detail/Config.cpp` resolves to a key whose spelling is guaranteed by one of these constants.