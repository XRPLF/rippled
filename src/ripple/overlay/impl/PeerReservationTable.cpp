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

#include <ripple/overlay/PeerReservationTable.h>

#include <ripple/core/DatabaseCon.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/jss.h>

#include <boost/optional.hpp>

#include <string>

namespace ripple {

auto
PeerReservation::toJson() const -> Json::Value
{
    Json::Value result{Json::objectValue};
    result[jss::node] = toBase58(TokenType::NodePublic, nodeId_);
    if (description_)
    {
        result[jss::description] = *description_;
    }
    return result;
}

// See `ripple/app/main/DBInit.cpp` for the `CREATE TABLE` statement.
// REVIEWER: It is unfortunate that we do not get to define a function for it.

// REVIEWER: We choose a `bool` return type to fit in with the error handling
// scheme of other functions called from `ApplicationImp::setup`, but we
// always return "no error" (`true`) because we can always return an empty
// table.

bool
PeerReservationTable::load(DatabaseCon& connection)
{
    connection_ = &connection;
    auto db = connection_->checkoutDb();

    boost::optional<std::string> valPubKey, valDesc;
    // REVIEWER: We should really abstract the table and column names into
    // constants, but no one else does. Because it is too tedious?
    soci::statement st =
        (db->prepare << "SELECT PublicKey, Description FROM PeerReservations;",
         soci::into(valPubKey),
         soci::into(valDesc));
    st.execute();
    while (st.fetch())
    {
        if (!valPubKey)
        {
            // REVIEWER: How to signal unreachable? This represents a `NULL`
            // in a `NOT NULL` column.
        }
        auto const optNodeId =
            parseBase58<PublicKey>(TokenType::NodePublic, *valPubKey);
        if (!optNodeId)
        {
            // REVIEWER: Does the call site filter the level?
            // Where is the documentation for "how to use Journal"?
            if (auto stream = journal_.warn())
            {
                stream << "load: not a public key: " << valPubKey;
            }
            // TODO: Remove any invalid public keys?
        }
        auto const& nodeId = *optNodeId;
        table_.emplace(nodeId, PeerReservation{nodeId, valDesc});
    }

    return true;
}

auto
PeerReservationTable::upsert(
    PublicKey const& nodeId,
    boost::optional<std::string> const& desc)
    -> boost::optional<PeerReservation>
{
    PeerReservation const rvn{nodeId, desc};
    boost::optional<PeerReservation> previous;

    auto emplaced = table_.emplace(nodeId, rvn);
    if (!emplaced.second)
    {
        // The node already has a reservation.
        // I think most people just want to overwrite existing reservations.
        // TODO: Make a formal team decision that we do not want to raise an
        // error upon a collision.
        previous = emplaced.first->second;
        emplaced.first->second = rvn;
    }

    auto db = connection_->checkoutDb();
    *db << "INSERT INTO PeerReservations (PublicKey, Description) "
           "VALUES (:nodeId, :desc) "
           "ON CONFLICT (PublicKey) DO UPDATE SET "
           "Description=excluded.Description",
        soci::use(toBase58(TokenType::NodePublic, nodeId)), soci::use(desc);

    return previous;
}

auto
PeerReservationTable::erase(PublicKey const& nodeId)
    -> boost::optional<PeerReservation>
{
    boost::optional<PeerReservation> previous;

    auto const it = table_.find(nodeId);
    if (it != table_.end())
    {
        previous = it->second;
        table_.erase(it);
        auto db = connection_->checkoutDb();
        *db << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId",
            soci::use(toBase58(TokenType::NodePublic, nodeId));
    }

    return previous;
}

}  // namespace ripple
