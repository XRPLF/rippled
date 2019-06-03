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
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/handlers/Handlers.h>

#include <boost/optional.hpp>

#include <string>
#include <utility>

namespace ripple {

Json::Value
doReservationsAdd(RPC::Context& context)
{
    auto const& params = context.params;

    if (!params.isMember(jss::public_key))
        return RPC::missing_field_error(jss::public_key);

    // Returning JSON from every function ruins any attempt to encapsulate
    // the pattern of "get field F as type T, and diagnose an error if it is
    // missing or malformed":
    // - It is costly to copy whole JSON objects around just to check whether an
    //   error code is present.
    // - It is not as easy to read when cluttered by code to pack and unpack the
    //   JSON object.
    // - It is not as easy to write when you have to include all the packing and
    //   unpacking code.
    // Exceptions would be easier to use, but have a terrible cost for control flow.
    // An error monad is purpose-built for this situation; it is essentially
    // an optional (the "maybe monad" in Haskell) with a non-unit type for the
    // failure case to capture more information.
    if (!params[jss::public_key].isString())
        return RPC::expected_field_error(jss::public_key, "a string");

    // Same for the pattern of "if field F is present, make sure it has type T
    // and get it".
    boost::optional<std::string> name;
    if (params.isMember(jss::name))
    {
        if (!params[jss::name].isString())
            return RPC::expected_field_error(jss::name, "a string");
        name = params[jss::name].asString();
    }

    // channel_verify takes a key in both base58 and hex.
    // @nikb prefers that we take only base58.
    boost::optional<PublicKey> optPk = parseBase58<PublicKey>(
        TokenType::NodePublic,
        params[jss::public_key].asString());
    if (!optPk)
        return rpcError(rpcPUBLIC_MALFORMED);
    PublicKey const& identity = *optPk;

    // I think most people just want to overwrite existing reservations.
    // TODO: Make a formal team decision that we do not want to raise an
    // error upon a collision.
    auto emplaced = context.app.peerReservations().emplace(
        std::make_pair(identity, PeerReservation{identity, name}));
    if (!emplaced.second)
        // The public key already has a reservation. Overwrite it.
        emplaced.first->second.name_ = name;

    Json::Value result;
    // TODO: Need to indicate whether it was inserted or overwritten.
    return result;
}

Json::Value
doReservationsDel(RPC::Context& context)
{
    Json::Value result;
    return result;
}

Json::Value
doReservationsList(RPC::Context& context)
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
        // jaReservation[jss::public_key] = reservation.identity_;
        if (reservation.name_) {
            jaReservation[jss::name] = *reservation.name_;
        }
        jaReservations.append(jaReservation);
    }
    return result;
}

}
