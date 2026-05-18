# `src/xrpld/app/rdb/PeerFinder.h`

## Role and Purpose

This header is the public SQL boundary for the XRP Ledger's peer-discovery bootstrap cache. It declares four free functions that encapsulate all SQLite access needed to persist, migrate, and retrieve the list of known peers that the `PeerFinder` subsystem uses on startup to locate the network. The design deliberately separates SQL mechanics from the abstract `PeerFinder::Store` interface, keeping persistence logic out of the core networking abstraction.

## Relationship to Surrounding Architecture

Three components work together here. `PeerFinder::Store` (in `peerfinder/detail/Store.h`) is a pure abstract interface defining `load()` and `save()` — it knows nothing about SQL. `StoreSqdb` (in `peerfinder/detail/StoreSqdb.h`) is the concrete SQLite implementation of that interface; it owns a `soci::session` and delegates all SQL work to the four functions declared in this header. The `detail/PeerFinder.cpp` translation unit provides the bodies. This layering means the networking subsystem can be tested against mock `Store` implementations without any database involvement, while the SQL layer can be audited and changed independently.

## The Four Functions

`initPeerFinderDB` opens a SOCI database session using connection parameters drawn from the node's `BasicConfig` under the `"peerfinder"` section, then creates the `SchemaVersion` tracking table and the `PeerFinder_BootstrapCache` table (address TEXT UNIQUE, valence INTEGER) if they do not already exist. The entire setup is wrapped in a `soci::transaction` so schema creation is atomic — a half-initialized database cannot be left behind by a crash during first startup.

`updatePeerFinderDB` implements schema migration against the `currentSchemaVersion` constant (currently `4`, defined as an enum in `StoreSqdb`). It reads the stored version from `SchemaVersion`, then applies version-gated migration blocks in descending order. The most significant migration, triggered when the on-disk version is below 4, removes the historical `uptime` column from the bootstrap cache. Because older SQLite does not support `DROP COLUMN`, the function follows the standard SQLite workaround: create a replacement table without the unwanted column, copy all valid rows, drop the original, rename the replacement, and recreate the index. Bad address strings encountered during the copy are logged and skipped rather than aborting the migration. Versions below 3 trigger removal of several legacy endpoint tables that have no modern counterpart. After all migrations the function writes the current schema version back to `SchemaVersion` using `INSERT OR REPLACE`. A `THROW` is used if the on-disk version exceeds `currentSchemaVersion`, which would imply a downgrade scenario the code does not attempt to handle.

A notable SOCI quirk is documented in the implementation: querying an optional integer requires `boost::optional<int>` rather than `std::optional<int>` because the SOCI binding layer predates C++17 and lacks specializations for the standard type. This is a narrow dependency worth knowing when maintaining migration code.

`readPeerFinderDB` fetches every row from `PeerFinder_BootstrapCache` and fires a caller-provided `std::function<void(std::string const&, int)>` for each one. Returning a callback rather than a container keeps the raw string-to-`beast::IP::Endpoint` conversion — and the associated validity filtering — in `StoreSqdb::load()` where it belongs, and avoids allocating an intermediate vector of strings.

`savePeerFinderDB` persists the current in-memory bootstrap cache by executing a full `DELETE FROM PeerFinder_BootstrapCache` followed by a bulk `INSERT` of the supplied `PeerFinder::Store::Entry` vector, all inside a single transaction. The replacement-rather-than-merge approach is intentional: the bootstrap cache is treated as an opaque snapshot, not a partially-updated set. Using SOCI's vector binding (`soci::use(s)`, `soci::use(valence)`) issues the inserts as a single prepared statement over parallel address and valence arrays, which is meaningfully faster than row-by-row iteration for a cache that could contain hundreds of peers.

## Design Observations

The header's choice of free functions over a class is consistent with the `rdb/` module convention: these functions are stateless SQL helpers, and all state (the `soci::session`, the journal, the schema version constant) lives in `StoreSqdb`. The separation makes the SQL logic easy to unit-test in isolation by constructing a throw-away in-memory session. The `initPeerFinderDB` / `updatePeerFinderDB` split, rather than a single `openAndMigrate` function, also allows `StoreSqdb::open()` to call them in sequence while keeping each step's logic self-contained.