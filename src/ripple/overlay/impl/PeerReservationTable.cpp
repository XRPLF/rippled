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

#include <ripple/basics/Log.h>
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
    result[jss::node] = toBase58(TokenType::NodePublic, nodeId);
    if (description)
    {
        result[jss::description] = *description;
    }
    return result;
}

// See `ripple/app/main/DBInit.cpp` for the `CREATE TABLE` statement.
// It is unfortunate that we do not get to define a function for it.

// We choose a `bool` return type to fit in with the error handling scheme
// of other functions called from `ApplicationImp::setup`, but we always
// return "no error" (`true`) because we can always return an empty table.
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
            JLOG(journal_.warn()) << "load: not a public key: " << valPubKey;
            // TODO: Remove any invalid public keys?
        }
        table_.insert(PeerReservation{*optNodeId, valDesc});
    }

    return true;
}

auto
PeerReservationTable::upsert(
    PublicKey const& nodeId,
    boost::optional<std::string> const& desc)
    -> boost::optional<PeerReservation>
{
    boost::optional<PeerReservation> previous;

    // TODO: When C++17 arrives, replace with a structured binding to better
    // variable names.
    auto inserted = table_.insert(PeerReservation{nodeId, desc});
    if (!inserted.second)
    {
        // The node already has a reservation. Overwrite its description.
        // `std::unordered_set` does not have an `upsert` method, and sadly
        // makes it impossible for us to implement one efficiently:
        // https://stackoverflow.com/q/49651835/618906
        previous = *inserted.first;
        inserted.first->description = desc;
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

    auto const it = table_.find({nodeId});
    if (it != table_.end())
    {
        previous = *it;
        table_.erase(it);
        auto db = connection_->checkoutDb();
        *db << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId",
            soci::use(toBase58(TokenType::NodePublic, nodeId));
    }

    return previous;
}

}  // namespace ripple
