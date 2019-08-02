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
#include <ripple/protocol/PublicKey.h>

namespace ripple {

// See `ripple/app/main/DBInit.cpp` for the `CREATE TABLE` statement.
// REVIEWER: It is unfortunate that we do not get to define a function for it.

// REVIEWER: We choose a `bool` return type to fit in with the error handling
// scheme of other functions called from `ApplicationImp::setup`, but we
// always return "no error" (`true`) because we can always return an empty
// table.

bool
PeerReservationTable::load(DatabaseCon& connection)
{
    auto db = connection_->checkoutDb();

    boost::optional<std::string> valPubKey, valName;
    // REVIEWER: We should really abstract the table and column names into
    // constants, but no one else does. Because it is too tedious?
    soci::statement st =
        (db->prepare << "SELECT PublicKey, Name FROM PeerReservations;",
         soci::into(valPubKey),
         soci::into(valName));
    if (!st.execute())
    {
        return false;
    }
    while (st.fetch())
    {
        auto const pk = parseBase58<PublicKey>(
            TokenType::NodePublic, valPubKey.value_or(""));
        if (!pk)
        {
            // REVIEWER: Does the call site filter the level?
            // Where is the documentation for "how to use Journal"?
            if (auto stream = journal_.warn())
            {
                stream << "load: not a public key: " << varPubKey;
            }
            // TODO: Remove any invalid public keys?
        }
        PeerReservation const res{.nodeId_ = *pk, .name_ = valName};
        table_.emplace(*pk, res);
    }

    return true;
}

bool PeerReservationTable::upsert(
        PublicKey const& nodeId,
        boost::optional<std::string> const& name
)
{
    auto emplaced = table_.emplace(
        std::make_pair(nodeId, PeerReservation{nodeId, name}));
    if (!emplaced.second)
        // The node already has a reservation.
        // I think most people just want to overwrite existing reservations.
        // TODO: Make a formal team decision that we do not want to raise an
        // error upon a collision.
        emplaced.first->second.name_ = name;

    auto db = connection_->checkoutDb();
    *db << "INSERT INTO PeerReservations (PublicKey, Name) "
        "VALUES (:nodeId, :name) "
        "ON CONFLICT(PublicKey) DO UPDATE SET Name=excluded.Name",
        use(nodeId), use(name);

    return emplaced.second;
}

bool PeerReservationTable::erase(PublicKey const& nodeId)
{
    bool const removed = table_.erase(nodeId) > 0;

    auto db = connection_->checkoutDb();
    *db << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId", use(nodeId);

    return removed;
}

}  // namespace ripple
