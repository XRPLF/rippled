#pragma once

#include <xrpld/app/rdb/PeerFinder.h>
#include <xrpld/peerfinder/detail/Store.h>

#include <xrpl/rdb/SociDB.h>

namespace xrpl::PeerFinder {

/** SQLite-backed persistence adapter for the PeerFinder bootstrap cache.
 *
 *  Implements the `Store` interface by delegating all SQL to the free
 *  functions in `xrpld/app/rdb/PeerFinder.h` (`initPeerFinderDB`,
 *  `updatePeerFinderDB`, `readPeerFinderDB`, `savePeerFinderDB`).
 *  `StoreSqdb` itself contains no SQL literals; it is a thin adapter that
 *  wires the `Store` virtual interface to the `rdb` layer, passing
 *  `sqlDb_` as the shared connection handle.
 *
 *  The `soci::session` is a value member — the SQLite connection is open
 *  for the lifetime of the object and requires no external synchronization
 *  beyond what PeerFinder's `Logic` mutex already provides.
 *
 *  Typical call sequence after construction:
 *  1. `open(config)` — opens the database file, creates tables, migrates schema.
 *  2. `load(cb)`     — streams persisted entries into `Bootcache` at startup.
 *  3. `save(v)`      — called periodically by `Bootcache::periodicActivity()`.
 *
 *  @see Store, Bootcache, Logic, initPeerFinderDB, updatePeerFinderDB
 */
class StoreSqdb : public Store
{
private:
    beast::Journal journal_;
    soci::session sqlDb_;

public:
    /** On-disk schema version this binary understands.
     *
     *  `updatePeerFinderDB` applies migrations from any stored version up to
     *  this value.  If the stored version exceeds this constant, `update()`
     *  throws `std::runtime_error` to prevent a stale binary from silently
     *  corrupting a database written by a newer one.
     *
     *  Migration history:
     *  - v1–v2: removed legacy endpoint tables (`LegacyEndpoints` family).
     *  - v3→v4: dropped the `uptime` column from `PeerFinder_BootstrapCache`
     *      via create-copy-drop-rename because SQLite does not support
     *      `DROP COLUMN`.
     */
    static constexpr auto kCURRENT_SCHEMA_VERSION = 4;

    /** Construct with an optional journal for diagnostic logging.
     *
     *  @param journal  Structured logger; defaults to the null sink so
     *      callers that do not need log output can omit the argument.
     */
    explicit StoreSqdb(beast::Journal journal = beast::Journal{beast::Journal::getNullSink()})
        : journal_(journal)
    {
    }

    ~StoreSqdb() override = default;

    /** Open the database and bring the schema up to date.
     *
     *  Calls `initPeerFinderDB` to open the SQLite file specified in
     *  @p config, creating the `PeerFinder_BootstrapCache` and
     *  `SchemaVersion` tables if they do not exist, then calls `update()`
     *  to apply any pending schema migrations.  Must be called once before
     *  `load()` or `save()`.
     *
     *  @param config  Application config containing the database path and
     *      other connection parameters.
     */
    void
    open(BasicConfig const& config)
    {
        init(config);
        update();
    }

    /** Stream all persisted bootstrap entries to the caller via callback.
     *
     *  Reads every row from `PeerFinder_BootstrapCache` and invokes @p cb
     *  for each one whose address string parses to a valid, specified
     *  `beast::IP::Endpoint`.  Rows with malformed or unspecified addresses
     *  are silently skipped with a `journal_.error()` log entry — they are
     *  never forwarded to the callback.
     *
     *  @param cb  Invoked once per valid entry with its endpoint and valence.
     *  @return    The number of valid entries passed to @p cb.
     */
    std::size_t
    load(load_callback const& cb) override
    {
        std::size_t n(0);

        readPeerFinderDB(sqlDb_, [&](std::string const& s, int valence) {
            beast::IP::Endpoint const endpoint(beast::IP::Endpoint::fromString(s));

            if (!isUnspecified(endpoint))
            {
                cb(endpoint, valence);
                ++n;
            }
            else
            {
                JLOG(journal_.error()) << "Bad address string '" << s << "' in Bootcache table";
            }
        });

        return n;
    }

    /** Atomically replace the entire persisted bootstrap cache.
     *
     *  Issues `DELETE FROM PeerFinder_BootstrapCache` followed by a bulk
     *  `INSERT` of all entries in @p v, inside a single SOCI transaction.
     *  This is a full snapshot replace — not an upsert or diff — so the
     *  table's contents after the call exactly mirror @p v.
     *
     *  @param v  Complete snapshot of the current in-memory bootstrap cache,
     *      typically produced by `Bootcache` draining its bimap.
     */
    void
    save(std::vector<Entry> const& v) override
    {
        savePeerFinderDB(sqlDb_, v);
    }

    /** Migrate the on-disk schema to `kCURRENT_SCHEMA_VERSION`.
     *
     *  Reads the stored version number from `SchemaVersion` and applies
     *  any outstanding migrations in sequence.  Throws `std::runtime_error`
     *  if the stored version is higher than `kCURRENT_SCHEMA_VERSION`.
     *  Called automatically by `open()`; exposed publicly for testing.
     */
    void
    update()
    {
        updatePeerFinderDB(sqlDb_, kCURRENT_SCHEMA_VERSION, journal_);
    }

private:
    void
    init(BasicConfig const& config)
    {
        initPeerFinderDB(sqlDb_, config, journal_);
    }
};

}  // namespace xrpl::PeerFinder
