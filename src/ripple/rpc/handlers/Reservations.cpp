//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/handlers/Handlers.h>

namespace ripple {

Json::Value
doReservationsAdd(RPC::Context& context)
{
    // TODO:
    // 1. Get the parameters of the method.
    // 2. Check that the parameters have the right types.
    // 3. Check if the node already has a reservation.
    // 4. Add the reservation to context.app.peerReservations().
    Json::Value result;
    return result;
}

Json::Value
doReservationsDel(RPC::Context& context)
{
    Json::Value result;
    return result;
}

Json::Value
doReservationsLs(RPC::Context& context)
{
    auto const& reservations = context.app.peerReservations();
    // Enumerate the reservations in context.app.peerReservations()
    // as a Json::Value.
    Json::Value result;
    Json::Value& jaReservations = (result["reservations"] = Json::arrayValue);
    for (auto const& pair : reservations)
    {
        auto const& reservation = pair.second;
        Json::Value jaReservation;
        // TODO: Base58 encode the public key?
        // jaReservation["public_key"] = reservation.identity_;
        if (reservation.name_) {
            jaReservation["name"] = *reservation.name_;
        }
        jaReservations.append(jaReservation);
    }
    return result;
}

}