/**
 * @file PeerReservationTable.cpp
 * @brief Persistent peer slot registry for the XRPL overlay.
 *
 * Implements `PeerReservation::toJson()` and all four public methods of
 * `PeerReservationTable`. Reserved nodes bypass the normal slot limit, so
 * validators and other trusted peers can always connect even when the overlay
 * is at capacity. The in-memory `unordered_set` is loaded from SQLite via
 * `getPeerReservationTable` on startup and kept synchronized with the database
 * through `insertPeerReservation`/`deletePeerReservation` on every mutation.
 *
 * @note All four public methods of `PeerReservationTable` acquire `mutex_`
 *     before touching `table_`, making the class safe for concurrent use by
 *     overlay inbound-connection handling and RPC admin handlers.
 */

#include <xrpl/core/PeerReservationTable.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Wallet.h>

#include <algorithm>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

/**
 * Serialize this reservation to a JSON object for RPC responses.
 *
 * Encodes `nodeId` as a Base58-encoded node public key string under the
 * `node` key — the canonical XRPL representation used in config files and
 * peer protocol messages. The `description` field is omitted when empty to
 * keep the JSON compact.
 *
 * @return A JSON object with at least `{"node": "<base58-public-key>"}` and,
 *     when non-empty, `{"description": "..."}`.
 */
auto
PeerReservation::toJson() const -> json::Value
{
    json::Value result{json::ValueType::Object};
    result[jss::node] = toBase58(TokenType::NodePublic, nodeId);
    if (!description.empty())
    {
        result[jss::description] = description;
    }
    return result;
}

/**
 * Return a sorted snapshot of all current reservations.
 *
 * Copies the internal set into a vector while holding `mutex_`, then releases
 * the lock before sorting. The lock is intentionally released before
 * `std::sort` to keep the critical section short; callers receive a
 * point-in-time snapshot that may be momentarily stale, which is acceptable
 * for informational RPC responses.
 *
 * @return All reservations ordered by `nodeId` via `operator<`.
 */
auto
PeerReservationTable::list() const -> std::vector<PeerReservation>
{
    std::vector<PeerReservation> list;
    {
        std::scoped_lock const lock(mutex_);
        list.reserve(table_.size());
        std::ranges::copy(table_, std::back_inserter(list));
    }
    std::sort(list.begin(), list.end());  // NOLINT(modernize-use-ranges)
    return list;
}

// See `include/xrpl/rdb/DBInit.h` for the `CREATE TABLE` statement.

/**
 * Wire the table to its database connection and load persisted reservations.
 *
 * This is the second phase of a two-phase construction: `ApplicationImp`
 * constructs the table before the database is ready, then calls `load()` during
 * `setup()` once `DatabaseCon` is available. Stores the raw `connection`
 * pointer for subsequent mutations — the pointer must remain valid for the
 * lifetime of this object. Bulk-reads existing rows via
 * `getPeerReservationTable` and merges them into the in-memory set.
 *
 * @param connection The SQLite database connection that backs the
 *     `PeerReservations` table. Must outlive this `PeerReservationTable`.
 * @return Always `true`. A DB read failure yields an empty table rather than
 *     aborting startup, because the application can always reconcile state
 *     later — an empty reservation table is a valid initial state.
 * @note `insertOrAssign` and `erase` must not be called before `load()`.
 */
bool
PeerReservationTable::load(DatabaseCon& connection)
{
    std::scoped_lock const lock(mutex_);

    connection_ = &connection;
    auto db = connection.checkoutDb();
    auto table = getPeerReservationTable(*db, journal_);
    table_.insert(table.begin(), table.end());

    return true;
}

/**
 * Upsert a reservation, returning any displaced entry.
 *
 * If a reservation for `reservation.nodeId` already exists, it is removed and
 * replaced with the new one; otherwise the new reservation is inserted fresh.
 * `std::unordered_set` has no native `insert_or_assign`, so an erase-then-
 * insert sequence is used. The iterator is advanced (not decremented) before
 * erasing to obtain a safe insertion hint — decrement is illegal when the
 * element is at `begin()`, whereas incrementing to `end()` is always valid.
 * The database layer uses an SQL upsert (`INSERT ... ON CONFLICT ... DO UPDATE`)
 * for idempotency, mirroring the in-memory behaviour.
 *
 * @param reservation The reservation to add or update.
 * @return The displaced `PeerReservation` if one existed for the same
 *     `nodeId`, or `std::nullopt` for a fresh insertion.
 * @throw soci::soci_error on database failure.
 */
std::optional<PeerReservation>
PeerReservationTable::insertOrAssign(PeerReservation const& reservation)
{
    std::optional<PeerReservation> previous;

    std::scoped_lock const lock(mutex_);

    auto hint = table_.find(reservation);
    if (hint != table_.end())
    {
        // `std::unordered_set` does not have an `insertOrAssign` method,
        // and sadly makes it impossible for us to implement one efficiently:
        // https://stackoverflow.com/q/49651835/618906
        // Regardless, we don't expect this function to be called often, or
        // for the table to be very large, so this less-than-ideal
        // remove-then-insert is acceptable in order to present a better API.
        previous = *hint;
        // Increment rather than decrement: decrement is illegal when the found
        // element is at begin(), while incrementing to end() is always legal
        // and still provides a valid insertion hint.
        auto const deleteme = hint;
        ++hint;
        table_.erase(deleteme);
    }
    table_.insert(hint, reservation);

    auto db = connection_->checkoutDb();
    insertPeerReservation(*db, reservation.nodeId, reservation.description);

    return previous;
}

/**
 * Remove a reservation by node public key, returning the erased entry.
 *
 * The `DELETE` SQL is only issued when the key is found in the in-memory set,
 * avoiding a no-op database round-trip for unknown node IDs.
 *
 * @param nodeId The node public key identifying the reservation to remove.
 * @return The removed `PeerReservation` if it existed, or `std::nullopt` if
 *     no reservation for `nodeId` was present.
 */
std::optional<PeerReservation>
PeerReservationTable::erase(PublicKey const& nodeId)
{
    std::optional<PeerReservation> previous;

    std::scoped_lock const lock(mutex_);

    auto const it = table_.find({.nodeId = nodeId});
    if (it != table_.end())
    {
        previous = *it;
        table_.erase(it);
        auto db = connection_->checkoutDb();
        deletePeerReservation(*db, nodeId);
    }

    return previous;
}

}  // namespace xrpl
