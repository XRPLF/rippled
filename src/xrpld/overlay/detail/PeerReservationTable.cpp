#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/overlay/PeerReservationTable.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace ripple {

auto
PeerReservation::toJson() const -> Json::Value
{
    Json::Value result{Json::objectValue};
    result[jss::node] = toBase58(TokenType::NodePublic, nodeId);
    if (!description.empty())
    {
        result[jss::description] = description;
    }
    return result;
}

auto
PeerReservationTable::list() const -> std::vector<PeerReservation>
{
    std::vector<PeerReservation> list;
    {
        std::lock_guard lock(mutex_);
        list.reserve(table_.size());
        std::copy(table_.begin(), table_.end(), std::back_inserter(list));
    }
    std::sort(list.begin(), list.end());
    return list;
}

// See `ripple/app/main/DBInit.cpp` for the `CREATE TABLE` statement.
// It is unfortunate that we do not get to define a function for it.

// We choose a `bool` return type to fit in with the error handling scheme
// of other functions called from `ApplicationImp::setup`, but we always
// return "no error" (`true`) because we can always return an empty table.
bool
PeerReservationTable::load(DatabaseCon& connection)
{
    std::lock_guard lock(mutex_);

    connection_ = &connection;
    auto db = connection.checkoutDb();
    auto table = getPeerReservationTable(*db, journal_);
    table_.insert(table.begin(), table.end());

    return true;
}

std::optional<PeerReservation>
PeerReservationTable::insert_or_assign(PeerReservation const& reservation)
{
    std::optional<PeerReservation> previous;

    std::lock_guard lock(mutex_);

    auto hint = table_.find(reservation);
    if (hint != table_.end())
    {
        // The node already has a reservation. Remove it.
        // `std::unordered_set` does not have an `insert_or_assign` method,
        // and sadly makes it impossible for us to implement one efficiently:
        // https://stackoverflow.com/q/49651835/618906
        // Regardless, we don't expect this function to be called often, or
        // for the table to be very large, so this less-than-ideal
        // remove-then-insert is acceptable in order to present a better API.
        previous = *hint;
        // We should pick an adjacent location for the insertion hint.
        // Decrementing may be illegal if the found reservation is at the
        // beginning. Incrementing is always legal; at worst we'll point to
        // the end.
        auto const deleteme = hint;
        ++hint;
        table_.erase(deleteme);
    }
    table_.insert(hint, reservation);

    auto db = connection_->checkoutDb();
    insertPeerReservation(*db, reservation.nodeId, reservation.description);

    return previous;
}

std::optional<PeerReservation>
PeerReservationTable::erase(PublicKey const& nodeId)
{
    std::optional<PeerReservation> previous;

    std::lock_guard lock(mutex_);

    auto const it = table_.find({nodeId});
    if (it != table_.end())
    {
        previous = *it;
        table_.erase(it);
        auto db = connection_->checkoutDb();
        deletePeerReservation(*db, nodeId);
    }

    return previous;
}

}  // namespace ripple
