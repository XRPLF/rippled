# `StoreSqdb.h` — SQLite-backed Bootstrap Cache Persistence

## Role in the System

`StoreSqdb` is the SQLite persistence implementation for the PeerFinder subsystem's bootstrap cache. PeerFinder manages how an XRPL node discovers and connects to peers; its bootstrap cache is the on-disk record of previously known peer endpoints and their associated *valence* scores (a signed integer reflecting how reliable a peer has proven to be). Without persistence, a node that restarts cold has no memory of peers it successfully contacted before, forcing it back to hardcoded seeds. `StoreSqdb` solves this by writing and reading a SQLite table via SOCI.

## Class Structure and Design

`StoreSqdb` is a concrete subclass of the abstract `Store` interface defined in `Store.h`. That interface is deliberately minimal: it mandates only two operations — `load(load_callback)` to enumerate persisted entries and `save(std::vector<Entry>)` to overwrite them. Everything above that contract — the choice of database engine, schema versioning, connection management — is an implementation detail owned entirely by `StoreSqdb`.

The class holds two private members: a `beast::Journal` for structured logging and a `soci::session` representing an open SQLite connection. The session is a value member (not a pointer), so the database connection lifetime is tied directly to the object lifetime. There is no external locking; PeerFinder's upper layers are assumed to serialize access.

## Initialization and Schema Migration

`open()` is the entry point after construction. It calls the private `init()` then `update()` in sequence. `initPeerFinderDB()` (defined in `app/rdb/detail/PeerFinder.cpp`) opens the SQLite file from the `BasicConfig`, creates the `SchemaVersion` and `PeerFinder_BootstrapCache` tables if they do not already exist, and adds an index on the `address` column — all wrapped in a single transaction. `updatePeerFinderDB()` then reads the stored schema version number and applies any necessary migrations up to `currentSchemaVersion = 4`.

The migration logic reveals the schema's history. Versions below 3 accumulated legacy endpoint tables (`LegacyEndpoints`, `PeerFinderLegacyEndpoints`, `PeerFinder_LegacyEndpoints`) that are simply dropped. The version-4 migration is more involved: an older schema included an `uptime` column in `PeerFinder_BootstrapCache` that was later removed. Because SQLite does not support `DROP COLUMN`, the migration creates a new table `PeerFinder_BootstrapCache_Next` with the cleaned schema, bulk-copies all valid rows into it, drops the old table and index, renames the new table, and recreates the index. This entire operation runs inside a single SOCI transaction so it is atomic. If the stored version is *higher* than `currentSchemaVersion`, the code throws `std::runtime_error` rather than silently operating on a newer, unknown schema — a conservative safety check that prevents data corruption from running stale code against a database written by a newer binary.

## Load and Save Semantics

`load()` streams rows from `PeerFinder_BootstrapCache` via a prepared SOCI statement and, for each row, invokes the caller-supplied callback with a parsed `beast::IP::Endpoint` and the stored valence. Parsing is validated: `beast::IP::Endpoint::from_string()` can return an unspecified endpoint for malformed strings, and `StoreSqdb` explicitly checks `is_unspecified()` before forwarding to the callback, logging a `journal.error()` for any bad address. This guards against corrupted or manually edited database rows silently injecting zero endpoints into the peer set.

`save()` takes a full snapshot approach: it issues `DELETE FROM PeerFinder_BootstrapCache` followed by a bulk `INSERT` of all entries, inside a single transaction. This is not an upsert or a diff — it completely replaces the table's contents on every call. The bulk insert exploits SOCI's vector binding (`soci::use(vectorOfStrings), soci::use(vectorOfInts)`), which batches all rows into a single parameterized statement rather than looping individual inserts. This is efficient for the expected scale of the bootstrap cache (hundreds of entries at most) and keeps the transaction short.

## Separation of Concerns

A notable design choice is that `StoreSqdb` delegates all SQL to free functions declared in `xrpld/app/rdb/PeerFinder.h`. `StoreSqdb` itself contains no SQL literals. This layering means the database logic can be tested or replaced independently of the `Store` interface, and the raw SQL lives in the `rdb` (relational database) layer, consistent with how other XRPL subsystems separate their persistence code. `StoreSqdb` is effectively an adapter that wires the `Store` virtual interface to the `rdb` free-function API, passing through its `soci::session` as the shared state.

## SOCI and `boost::optional`

One subtlety visible in the implementation is that SOCI's `INTO` clause requires `boost::optional`, not `std::optional`, for nullable columns — a legacy constraint of the SOCI library version used here. The schema version query in `updatePeerFinderDB()` explicitly comments on this, using `boost::optional<int>` and calling `.value_or(0)` to treat a missing row as version 0 (a brand-new database).