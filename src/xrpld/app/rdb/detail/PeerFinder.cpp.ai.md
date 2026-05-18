# `src/xrpld/app/rdb/detail/PeerFinder.cpp`

This file is the SQLite persistence layer for the XRPL node's PeerFinder subsystem. PeerFinder is responsible for discovering and maintaining peer connections; to survive restarts without having to re-discover the network from scratch, it persists a "bootstrap cache" of known peer endpoints and their associated valence scores to disk. This file implements the four free functions that perform all DDL, migration, read, and write operations against that cache.

## Role in the System

The direct consumer of these functions is `StoreSqdb` (`src/xrpld/peerfinder/detail/StoreSqdb.h`), a concrete implementation of the abstract `PeerFinder::Store` interface. `StoreSqdb` owns a `soci::session` and delegates every database interaction to the four functions here. This separation keeps raw SQL out of the PeerFinder business logic and centralizes all schema knowledge in one file, consistent with how the rest of the XRPL codebase partitions database concerns under `src/xrpld/app/rdb/`.

## Schema

The database contains two tables. `SchemaVersion` maps a component name (here `'PeerFinder'`) to an integer version number, serving as a migration checkpoint. `PeerFinder_BootstrapCache` holds the actual data: an auto-incremented `id`, a `TEXT UNIQUE NOT NULL` `address` (a serialized `beast::IP::Endpoint`), and an `INTEGER` `valence` representing a peer's connection quality score. An index on `address` accelerates uniqueness checks and lookups.

## Initialization and Migration Split

`initPeerFinderDB` and `updatePeerFinderDB` are deliberately separate entry points, called in sequence by `StoreSqdb::open()`. Initialization uses `CREATE TABLE IF NOT EXISTS`, making it idempotent and safe on any database regardless of prior state. Migration, called immediately after, reads the stored schema version and applies forward-only patches. This design means a crash during initial schema creation leaves no half-initialized state (the entire `initPeerFinderDB` runs inside a single `soci::transaction`), and migration logic never needs to compensate for incomplete initialization.

## Migration Logic in `updatePeerFinderDB`

The current schema version is `4`, encoded as a compile-time enum constant in `StoreSqdb`. `updatePeerFinderDB` receives this value and compares it against whatever version is stored in the database:

- If the stored version is *higher* than expected, the code throws `std::runtime_error`. This is the forward-compatibility guard — an older binary must not silently corrupt a database written by a newer one.
- If the stored version is *lower*, the appropriate migration blocks run.

The `version < 4` block removes a now-defunct `uptime` column from `PeerFinder_BootstrapCache`. SQLite does not support `DROP COLUMN` (at least not in the versions targeted here), so the standard workaround is used: create a replacement table `PeerFinder_BootstrapCache_Next` with the new schema, copy all rows with address-string validation, drop the original, and rename. During the copy, each address string is parsed through `beast::IP::Endpoint::from_string` and checked with `is_unspecified()`; corrupted rows are logged and discarded rather than carried forward.

The `version < 3` block simply drops three legacy tables (`LegacyEndpoints`, `PeerFinderLegacyEndpoints`, `PeerFinder_LegacyEndpoints`) that existed in even older schema versions. The two blocks are independent and ordered so that a database at version 2 executes both migrations in a single transaction, while a database at version 3 only executes the `< 4` block.

After all applicable patches, the function upserts the current version into `SchemaVersion` using `INSERT OR REPLACE`, ensuring the next startup sees the updated version regardless of whether a prior version row existed.

## Read and Write Patterns

`readPeerFinderDB` uses a cursor-based iteration rather than loading all rows into a vector first. It prepares a `soci::statement`, calls `execute()`, then calls `fetch()` in a loop, invoking the caller-supplied `std::function<void(std::string const&, int)>` for each row. Passing a callback rather than returning a container keeps the string-to-endpoint conversion and validity filtering in `StoreSqdb::load()` where semantic ownership belongs, and avoids an intermediate heap allocation.

`savePeerFinderDB` implements a full-replacement strategy: it deletes all rows, then bulk-inserts the new set. This matches how `Bootcache` manages its in-memory state — it maintains a ranked set and periodically writes the whole thing to disk, so incremental diff-based persistence would add complexity without benefit. The bulk insert uses SOCI's vector binding (`soci::use(s), soci::use(valence)` where both are `std::vector`), which results in a single prepared statement executing across all rows rather than N individual INSERT calls. The entire operation is wrapped in `soci::transaction` to prevent a partial write from leaving the cache in an inconsistent state.

## SOCI Compatibility Note

The migration code includes a notable comment: SOCI requires `boost::optional<int>` rather than `std::optional<int>` for nullable output parameters. This is a known limitation of SOCI's type-binding layer at the versions used by this codebase, and the comment flags it explicitly to prevent well-meaning refactors from breaking compilation by switching to the standard library type.