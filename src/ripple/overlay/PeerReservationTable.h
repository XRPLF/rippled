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

#ifndef RIPPLE_OVERLAY_PEER_RESERVATION_TABLE_H_INCLUDED
#define RIPPLE_OVERLAY_PEER_RESERVATION_TABLE_H_INCLUDED

#include <ripple/beast/hash/uhash.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/protocol/PublicKey.h>

#include <boost/optional.hpp>

#include <string>
#include <unordered_map>

namespace ripple {

// Value type for reservations.
// REVIEWER: What is the preferred naming convention for member variables?
struct PeerReservation
{
public:
    PublicKey const identity_;
    boost::optional<std::string> name_;
};

class PeerReservationTable {
public:
    using table_type = std::unordered_map<PublicKey, PeerReservation, beast::uhash<>>;

    explicit
    PeerReservationTable(beast::Journal journal = beast::Journal(beast::Journal::getNullSink()))
        : journal_ (journal)
    {
    }

    table_type const& table() const {
        return table_;
    }

    // Because `ApplicationImp` has two-phase initialization, so must we.
    // Our dependencies are not prepared until the second phase.
    bool init(DatabaseCon& connection);

private:
    beast::Journal mutable journal_;
    // REVIEWER: What is the policy on forward declarations? We can use one
    // for DatabaseCon.
    DatabaseCon* connection_;
    table_type table_;

};

}

#endif
