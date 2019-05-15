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

/* Returns `true` and assigns `error` if there was an error.
   Returns `false` and assigns `pk` if there was not.
*/
// Alas, our no-exception RPC framework makes it difficult to
// abstract functions.
bool
parsePublicKey(
    Json::Value& error,
    PublicKey& pk,
    std::string const& string,
    TokenType tokenType)
{
    // channel_verify takes a key in both base58 and hex.
    // Am I understanding that right? We do the same.
    // TODO: Share this function with channel_verify
    // (by putting it in PublicKey.h).
    boost::optional<PublicKey> optPk = parseBase58<PublicKey>(tokenType, string);

    if (optPk)
    {
        pk = *optPk;
    } else {
        std::pair<Blob, bool> pair{strUnHex(string)};
        if (!pair.second) {
            error = rpcError(rpcPUBLIC_MALFORMED);
            return true;
        }
        auto const pkType = publicKeyType(makeSlice(pair.first));
        if (!pkType) {
            error = rpcError(rpcPUBLIC_MALFORMED);
            return true;
        }
        pk = PublicKey(makeSlice(pair.first));
    }

    return false;
}

Json::Value
doReservationsAdd(RPC::Context& context)
{
    auto const& params = context.params;

    if (!params.isMember("public_key"))
    {
        return RPC::missing_field_error("public_key");
    }

    Json::Value result;

    PublicKey identity;
    if (parsePublicKey(
            result,
            identity,
            // TODO: Can asString() fail?
            params["public_key"].asString(),
            TokenType::NodePublic))
        return result;

    const boost::optional<std::string> name = params.isMember("name")
        ? boost::make_optional(params["name"].asString())
        : boost::none;

    // I think most people just want to overwrite existing reservations.
    // TODO: Make a formal team decision that we do not want to raise an
    // error upon a collision.
    auto emplaced = context.app.peerReservations().emplace(
        std::make_pair(identity, overlay::PeerReservation{identity, name}));
    if (!emplaced.second)
        // The public key already has a reservation. Overwrite it.
        emplaced.first->second.name_ = name;

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