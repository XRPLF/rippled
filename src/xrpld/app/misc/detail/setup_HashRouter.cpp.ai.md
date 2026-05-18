# `setup_HashRouter.cpp` — HashRouter Configuration Parser

## Role in the System

`setup_HashRouter.cpp` sits in the `detail/` subfolder of `src/xrpld/app/misc/` and implements a single free function: `xrpl::setup_HashRouter(Config const&)`. Its sole responsibility is to read timing parameters from the `[hashrouter]` section of the node's configuration file, validate them against XRPL-specific invariants, and return a populated `HashRouter::Setup` struct that the `HashRouter` constructor will store and use for the lifetime of the process.

This follows a recurring pattern in the rippled codebase: each subsystem with configurable parameters has a corresponding `setup_*` function (usually in `detail/`) that owns the parsing and validation logic, keeping the subsystem class itself decoupled from the raw `Config` object.

## What `HashRouter::Setup` Controls

`HashRouter` is the peer-message deduplication table for the P2P overlay. When a transaction or validation arrives, its hash is stored in an `aged_unordered_map`; subsequent duplicate messages from any peer are suppressed using this table. Two timing fields in `HashRouter::Setup` control its behavior:

- **`holdTime`** (default 300 s): the expiration lifetime of a hash entry. Once an entry ages out of the map, the same hash can be accepted again — necessary for long-lived objects that might legitimately re-appear.
- **`relayTime`** (default 30 s): the minimum interval before a previously-relayed message may be relayed a second time. This dampens relay storms without permanently suppressing legitimate re-broadcasts.

## Parsing and Validation Logic

The function uses the `set()` utility from `BasicConfig.h` to optionally read each key from the config section. If a key is absent, `set()` returns `false` and the field retains its default value, so neither `hold_time` nor `relay_time` is mandatory in the config file.

When a value is present, three invariants are enforced and any violation throws `std::runtime_error`, aborting startup:

1. **`hold_time >= 12` seconds** — the error message calls this "the approximate validation time for three ledgers." The XRPL ledger closes roughly every 4 seconds, so 12 seconds ensures a hash entry outlives a full three-ledger validation cycle before it can be evicted.

2. **`relay_time >= 8` seconds** — similarly called "the approximate validation time for two ledgers." This floors the relay-dampening window at two consensus rounds to prevent re-relay storms that would swamp the network before validators have had a chance to converge.

3. **`relay_time <= hold_time`** — a cross-field consistency check. If `relayTime` exceeded `holdTime`, an entry could expire from the map before its relay-cooldown elapsed, making the relay throttle meaningless and opening a narrow window for duplicate relays of the same message.

The floor values are derived from ledger timing constants rather than arbitrary numbers. The comments in the exception messages make this explicit, tying the constraints to observable network behavior rather than internal implementation details.

## Why These Defaults Are Deliberately Undocumented

The `HashRouter::Setup` comment in the header states that while the fields are configurable, they are undocumented in the user-facing configuration guide. Changing them without network-wide coordination could cause nodes to disagree on how long to suppress duplicate messages, potentially fragmenting message propagation across the overlay. The `setup_HashRouter` function acts as the only gate between the config file and these values, making it the right place to enforce the minimum safety envelope even for operators who override the defaults.

## Relationship to Sibling Files

The declaration lives in `src/xrpld/app/misc/setup_HashRouter.h`, which simply forward-declares `Config` and re-exports the `HashRouter::Setup` return type from `<xrpl/core/HashRouter.h>`. The implementation in `detail/` follows the convention of hiding all `Config` parsing machinery from callers — application code that constructs a `HashRouter` only sees the strongly-typed `Setup` struct, never the raw string-map config section.