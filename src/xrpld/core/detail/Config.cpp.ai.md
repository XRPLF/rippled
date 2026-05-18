# `src/xrpld/core/detail/Config.cpp`

## Role in the System

`Config.cpp` is the implementation file for the XRPL node daemon's configuration subsystem. It is responsible for the full lifecycle of node configuration: discovering the config file on disk, parsing the INI-format text into sections, interrogating system hardware to choose sensible defaults, and populating the `Config` object's typed fields that the rest of `xrpld` reads throughout its lifetime. This file is the single place that translates the human-authored `xrpld.cfg` into the concrete values driving peer limits, cache sizes, fee votes, SQLite pragmas, and more.

## Cross-Platform Memory Detection

Three separate definitions of `detail::getMemorySize()` live inside `#if BOOST_OS_WINDOWS`, `#if BOOST_OS_LINUX`, and `#if BOOST_OS_MACOS` guards. Each queries the OS for total physical RAM using the appropriate API (`GlobalMemoryStatusEx` on Windows, `sysinfo` on Linux, `sysctl(CTL_HW, HW_MEMSIZE)` on macOS) and returns a `std::uint64_t` byte count. On failure, all three return `0` silently; the caller (`setupControl`) treats zero as "unknown" and defaults conservatively. The `[[nodiscard]]` attribute prevents unintentional discard of the return value. Placing these inside `xrpl::detail` and guarding with Boost's OS macros means none of the platform-specific headers bleed into the general build.

## The `sizedItems` Table and Node Size Autodetection

A file-local `constexpr` array of 13 `{SizedItem, array<int,5>}` pairs encodes five node size tiers — `tiny`, `small`, `medium`, `large`, `huge` — for every tuneable internal parameter (`treeCacheSize`, `ledgerSize`, `burstSize`, etc.). This design centralises all hardware-tier thresholds in one reviewable block rather than scattering magic numbers across subsystems.

A `static_assert` with a `constexpr` lambda verifies at compile time that the array order exactly matches the `SizedItem` enum ordinals. If someone adds an enum entry without updating the table, the build fails immediately.

`Config::setupControl()` uses `ramSize_` (total RAM in GB, computed at construction) to walk the `ramSizeGB` row of this table, finding the first tier whose RAM threshold exceeds the detected hardware. It then applies a second cap: `min(hardware_concurrency / 2, computed_tier)`. The intuition is that a machine with many cores but little RAM should be sized by RAM, and vice versa. Standalone mode skips autodetection and stays at `tiny` (tier 0), because a developer instance does not need large caches.

## INI Parsing Pipeline

`parseIniFile()` is a straightforward line-oriented parser. It normalises CR/LF and CR line endings to LF first, then processes each line: blank lines and `#`-prefixed comments are skipped; `[section_name]` lines start a new section; everything else is appended to the current section's string vector. The result is `IniFileSections`, a `map<string, vector<string>>`. The default section (lines before the first `[...]` header) is keyed by an empty string.

`getIniFileSection()` is a thin pointer accessor into this map. `getSingleSection()` adds a semantic constraint: it only succeeds if the section contains exactly one line, logging a warning and returning `false` for any other cardinality. This enforces the common pattern where scalar config values should appear exactly once; callers use the boolean return to decide whether to update their field.

## Config File Location Discovery

`Config::setup()` implements a prioritised search for the config file when no explicit `--conf` path is given. The search order is:

1. Current working directory, looking first for `xrpld.cfg` then the legacy `rippled.cfg`.
2. XDG Base Directory paths derived from `$HOME`, `$XDG_CONFIG_HOME`, and `$XDG_DATA_HOME` (with the XDG defaults applied if those variables are absent).
3. System-level `/etc/opt/<systemName>`.

The `do { ... } while (false)` idiom with `break` lets each candidate exit the search chain early, avoiding the need for nested `if` chains or gotos. Data directory discovery mirrors the config directory logic, defaulting to a `db/` subdirectory relative to wherever the config file was found, but overridable via `[database_path]` in the config itself.

After `load()` runs, `setup()` creates the data directory with `create_directories`, throwing a `std::runtime_error` if that fails. Standalone mode clears `dataDir` so no database directory is created at all.

## `loadFromString()`: The Main Parsing Logic

Separating `load()` (reads from disk) from `loadFromString()` (works on a raw string) is a deliberate testability decision: unit tests inject config content without touching the filesystem.

`loadFromString()` calls `parseIniFile()`, then calls the base-class `build()` to populate the generic `BasicConfig` section map, and then iterates through the known section names, materialising typed fields on the `Config` object. A shared `strTemp` string is used as an intermediate buffer for `getSingleSection()` calls, which is then lexically cast to the destination type using `beast::lexicalCastThrow`.

Several cross-cutting concerns are handled here:

- **IP address colon normalisation**: The `[ips]` and `[ips_fixed]` entries traditionally use space as an IP/port separator, but many admins write `host:port`. A regex replace converts the trailing `:port` suffix to ` port`, but carefully skips any line containing more than one colon (IPv6 addresses).

- **Validator configuration**: Validator keys and UNL list sites are not loaded in standalone mode. When a `[validators_file]` path is specified, that file is parsed with `parseIniFile` and its `[validators]`, `[validator_keys]`, and `[validator_list_keys]` sections are merged into the main config. The file must contain at least one of these sections or a `std::runtime_error` is thrown. The `[validator_list_threshold]` value is checked to not exceed the number of configured list keys.

- **Mutually exclusive validator config**: Having both `[validation_seed]` and `[validator_token]` in the same file is caught and rejected with an explicit error, as these represent two incompatible ways of specifying validator identity.

- **Feature flags**: The `[features]` section lists amendment names. Each is looked up in the registered feature registry; an unknown name throws rather than silently being ignored.

- **Relay policy**: `RELAY_UNTRUSTED_VALIDATIONS` and `RELAY_UNTRUSTED_PROPOSALS` use a three-way enum encoded as `1`/`0`/`-1` meaning "relay all", "relay trusted only", "drop". The strings `all`, `trusted`, and `drop_untrusted` map to these values.

- **`[reduce_relay]` deprecation**: A temporary code block handles the rename from `vp_enable` to `vp_base_squelch_enable`. Both are read and either can set the flag, but using both simultaneously in the same config file is an error. The code itself is annotated as temporary with prominent comments.

- **Network quorum sanity check**: After all peer limits are parsed, the code verifies that `NETWORK_QUORUM` does not exceed `PEERS_MAX`. This cross-field validation cannot be done field-by-field; it requires both values to be known first.

## `setup_DatabaseCon()` and SQLite Safety Levels

The free function `setup_DatabaseCon()` translates the `[sqlite]` config section into a `DatabaseCon::Setup` struct containing PRAGMA strings to be applied to every opened database. The design provides a `safety_level` high-level API (either `high` or `low`) that atomically sets `journal_mode`, `synchronous`, and `temp_store` together. If `safety_level` is present, mixing in individual settings for any of those three is forbidden and throws. This prevents partially-lowered durability settings that might not be intentional. When `low` safety is set on a node with significant ledger history (above `SQLITE_TUNING_CUTOFF`), a journal warning is emitted. The `page_size` must be a power of two between 512 and 65536; this is validated with a bitwise `(page_size & (page_size - 1)) != 0` check.

## Error Handling Strategy

The file consistently uses `Throw<std::runtime_error>()` — the XRPL codebase's `std::throw_with_nested`-based wrapper — for all validation failures. File-read errors in `load()` are treated as recoverable: they log to `stderr` and return without updating state, allowing the node to start with purely default values if no config file is found. This is a deliberate UX choice so that running `xrpld` with no config file in a development environment does not immediately crash. Parse errors and constraint violations in `loadFromString()`, however, are terminal.