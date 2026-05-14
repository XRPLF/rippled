#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

/** Return a snapshot of all configured peer reservations (ADMIN).
 *
 *  Accepts no input parameters. Delegates to `PeerReservationTable::list()`,
 *  which performs a mutex-guarded copy of the internal unordered set and
 *  returns it sorted, guaranteeing deterministic output on every call.
 *  Each reservation is serialized via `PeerReservation::toJson()`, which
 *  encodes the node public key as a base58 string and omits the
 *  `description` field when empty.
 *
 *  An empty reservation table produces `{"reservations": []}` rather than
 *  an error, because `PeerReservationTable::load()` always succeeds even
 *  when the backing SQLite table has no rows.
 *
 *  @param context  RPC dispatch context; `context.params` is not read.
 *  @return JSON object of the form `{"reservations": [...]}` where each
 *      element is a `PeerReservation` serialized by `toJson()`.
 */
json::Value
doPeerReservationsList(RPC::JsonContext& context)
{
    auto const& reservations = context.app.getPeerReservations().list();
    json::Value result{json::ValueType::Object};
    json::Value& jaReservations = result[jss::reservations] = json::ValueType::Array;
    for (auto const& reservation : reservations)
    {
        jaReservations.append(reservation.toJson());
    }
    return result;
}

}  // namespace xrpl
