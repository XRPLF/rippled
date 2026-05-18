# `src/xrpld/core/Config.h` — Server Configuration

`Config.h` declares the central configuration object for the `xrpld` node process. It defines `FeeSetup`, the `SizedItem` scaling enum, and the `Config` class itself, along with two free functions that extract specialized sub-configurations. Every major subsystem in the node receives a `const Config&` reference at startup, so this header is one of the most widely included files in the codebase.

## Inheritance and Architecture

`Config` inherits from `BasicConfig`, which is an INI-section store: it holds a map of named `Section` objects, each containing raw lines and key/value pairs parsed from the configuration file. The philosophy encoded directly in `Config.h` is that `Config` as a derived class is **deprecated**. The comment reads: *"This entire derived class is deprecated. For new config information use the style implied in the base class."* New subsystems are expected to fetch their own `Section` from `BasicConfig` and parse it locally, rather than adding new public members to `Config`. Despite this intent, `Config` still carries a large surface of public fields, reflecting accumulated organic growth.

## Loading Lifecycle

The three-stage loading process is initiated by `setup()`, which accepts the config file path string and three boolean mode flags. Internally it delegates to:

1. `setupControl(bQuiet, bSilent, bStandalone)` — sets operational mode flags and **auto-detects `NODE_SIZE`** from hardware. It reads system RAM using a platform-specific helper (`detail::getMemorySize()`, implemented separately for Linux, macOS, and Windows) and then cross-references the `ramSizeGB` row of the `sizedItems` table to pick an initial tier. The result is then capped by half the number of hardware threads so that a machine with many cores but little RAM doesn't over-commit. This auto-detection runs only in networked mode; standalone mode always defaults to `NODE_SIZE = 0` (tiny).

2. `load()` — locates and reads the config file, following a search priority: explicit `--conf` path → current working directory (checking both `xrpld.cfg` and the legacy `rippled.cfg`) → XDG config directories (`$XDG_CONFIG_HOME/<system>/`) → system defaults (`/etc/opt/<system>/`). File contents are read with `getFileContents()` and then forwarded to `loadFromString()`.

3. `loadFromString()` — the actual parsing workhorse. It calls `parseIniFile()` to convert text into an `IniFileSections` map, builds the `BasicConfig` section store, and then iterates over specific sections to populate `Config`'s typed members. This method is also called directly in unit tests via the public `loadFromString()` API, deliberately bypassing file I/O.

After `load()` returns, `setup()` initialises SSL contexts and, in standalone mode, forces `LEDGER_HISTORY = 0`.

## The `SizedItem` / `NODE_SIZE` Scaling System

The `SizedItem` enum indexes into a compile-time table of five-column arrays, where columns correspond to node sizes 0–4 (tiny, small, medium, large, huge). The table is defined in the `.cpp` as `sizedItems` and covers thirteen tunable quantities: sweep intervals, SHAMap tree cache sizes and ages, ledger cache sizes, database cache budgets, the open/final ledger limit, burst size, the RAM thresholds used for auto-detection, and the account-ID cache size.

`getValueFor(SizedItem item, std::optional<std::size_t> node)` looks up the appropriate column. If `node` is unseated it uses the configured `NODE_SIZE`. A `static_assert` in the implementation verifies that the enum ordinals match array positions, catching any future reordering at compile time. The design deliberately separates *what* to scale from *how much*: consumers call `getValueFor` without knowing what hardware tier the node is running on.

## `FeeSetup` and Fee Voting

`FeeSetup` holds the three baseline fee parameters: `reference_fee` (10 drops), `account_reserve` (10 XRP), and `owner_reserve` (2 XRP). These are the values the node will vote to establish on the ledger via the `FeeVote` mechanism during each voting ledger. `toFees()` converts the struct into a `Fees` object suitable for ledger construction. The free function `setup_FeeVote(Section const&)` reads these values from the `[voting]` config section, with the legacy `[fee_default]` section able to override `reference_fee` for offline signing workflows.

## Security-Sensitive Defaults

Two fields warrant attention because their defaults reflect explicit security choices:

- `signingEnabled_` is `false` by default and only exposed via `canSign()`. Allowing a public node to sign arbitrary transactions using submitted secret keys is a significant credential-exposure risk, and the server refuses to do so unless the operator explicitly sets `[signing_support] = 1` in the config.

- Validator nodes automatically receive `PATH_SEARCH_MAX = 0` during `loadFromString()` if either `[validation_seed]` or `[validator_token]` sections are present. Path-finding is computationally expensive and irrelevant for validators; allowing it wastes resources and could delay consensus-critical processing. The operator can override this explicitly.

## Peer Connectivity and Relay Controls

Peer limits are expressed in three overlapping fields. The legacy `PEERS_MAX` applies a single ceiling to all connections; the newer `PEERS_IN_MAX` and `PEERS_OUT_MAX` provide separate inbound/outbound ceilings. `loadFromString()` enforces that if either of the newer fields is configured, both must be present — partial configuration is rejected with an exception. If `PEERS_MAX` is set, the newer fields are ignored entirely, preserving backward compatibility.

Relay policy for untrusted validations and proposals uses a three-valued integer (`1` = relay all, `0` = relay trusted only, `-1` = drop completely), mapped from the human-readable strings `"all"`, `"trusted"`, and `"drop_untrusted"` in the config file.

## Experimental P2P Routing Features

The header contains two clearly annotated `!!TEMPORARY CODE BLOCK!!` zones controlling prototype reduce-relay algorithms: VP (validator/proposal) squelching and TX reduce-relay. These expose tunable knobs (`VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS`, `TX_RELAY_PERCENTAGE`, `TX_REDUCE_RELAY_MIN_PEERS`) that are expected to be removed once the underlying routing algorithm matures. The deprecation comments are unusually candid about this provisional state.

## `setup_DatabaseCon`

The free function `setup_DatabaseCon(Config const& c, std::optional<beast::Journal>)` reads the `[database_path]` and node-database sections from a `Config` to produce a `DatabaseCon::Setup` struct used by the SQLite backend. Declaring it here—rather than in a database-specific header—is a layering compromise: it requires `Config.h` to include `<xrpl/rdb/DatabaseCon.h>`, which the inline comment flags as a known levelization violation (`VFALCO Breaks levelization`), alongside the `<boost/filesystem.hpp>` include that similarly does not belong at this layer.