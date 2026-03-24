#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

Json::Value
doPeerReservationsList(RPC::JsonContext& context)
{
    auto const& reservations = context.app.peerReservations().list();
    // Enumerate the reservations in context.app.peerReservations()
    // as a Json::Value.
    Json::Value result{Json::objectValue};
    Json::Value& jaReservations = result[jss::reservations] = Json::arrayValue;
    for (auto const& reservation : reservations)
    {
        jaReservations.append(reservation.toJson());
    }
    return result;
}

}  // namespace xrpl
