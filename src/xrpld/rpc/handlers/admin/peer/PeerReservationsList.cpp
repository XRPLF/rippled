#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

json::Value
doPeerReservationsList(RPC::JsonContext& context)
{
    auto const& reservations = context.app.getPeerReservations().list();
    // Enumerate the reservations in context.app.getPeerReservations()
    // as a json::Value.
    json::Value result{json::ObjectValue};
    json::Value& jaReservations = result[jss::reservations] = json::ArrayValue;
    for (auto const& reservation : reservations)
    {
        jaReservations.append(reservation.toJson());
    }
    return result;
}

}  // namespace xrpl
