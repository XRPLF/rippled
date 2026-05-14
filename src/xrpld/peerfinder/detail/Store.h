#pragma once

namespace xrpl::PeerFinder {

/** Abstract persistence boundary for the PeerFinder bootstrap cache.
 *
 *  Defines the load/save contract between `Bootcache` (the in-memory ranked
 *  endpoint map) and durable storage. The only concrete production
 *  implementation is `StoreSqdb`, which delegates to SQLite via SOCI.
 *
 *  The interface is intentionally minimal: two pure virtual methods cover the
 *  two moments the in-memory cache crosses the storage boundary — initial load
 *  at startup and periodic full-replace saves triggered by
 *  `Bootcache::periodicActivity()`. All ranking, bimap management, and
 *  connection logic remain in `Bootcache` and `Logic`; this class never sees
 *  any of that.
 *
 *  @see Bootcache.h, StoreSqdb.h, Logic.h
 */
class Store
{
public:
    virtual ~Store() = default;

    /** Callback invoked once per valid entry during `load`.
     *
     *  @param endpoint  The peer's IP endpoint read from storage.
     *  @param valence   The persisted streak counter for that endpoint
     *      (positive = consecutive successes, negative = consecutive failures).
     */
    using load_callback = std::function<void(beast::IP::Endpoint, int)>;

    /** Stream all persisted bootstrap entries into the caller via callback.
     *
     *  Iterates over the durable store, invoking @p cb once for each valid
     *  entry. Using a callback rather than returning a container lets
     *  `Bootcache::load()` insert records directly into its bimap as they
     *  arrive, avoiding an intermediate allocation.
     *
     *  Implementations should silently skip malformed entries (e.g., bad
     *  address strings) and not invoke the callback for them; only valid,
     *  fully-parsed entries count toward the return value.
     *
     *  @param cb  Invoked for each valid entry with its endpoint and valence.
     *  @return    The number of valid entries passed to @p cb.
     */
    virtual std::size_t
    load(load_callback const& cb) = 0;

    /** A single bootstrap cache record as serialized to/from storage. */
    struct Entry
    {
        explicit Entry() = default;

        /** The peer's IP address and port. */
        beast::IP::Endpoint endpoint;

        /** Connection quality history for this endpoint.
         *
         *  Positive values count consecutive successful handshakes; negative
         *  values count consecutive failures. Higher valence entries are
         *  tried first by `Bootcache` when seeding outbound connections.
         */
        int valence{};
    };

    /** Atomically overwrite the entire persisted bootstrap cache.
     *
     *  Replaces whatever is currently in durable storage with the snapshot
     *  in @p v. This is a full replace, not an incremental update — the
     *  implementation is expected to clear the existing data before writing.
     *
     *  @param v  Flat snapshot of all current cache entries, typically
     *      produced by `Bootcache` draining its bimap.
     */
    virtual void
    save(std::vector<Entry> const& v) = 0;
};

}  // namespace xrpl::PeerFinder
