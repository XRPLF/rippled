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
#include <ripple/beast/utility/Journal.h>
#include <ripple/json/json_forwards.h>
#include <ripple/protocol/PublicKey.h>

#define SOCI_USE_BOOST
#include <boost/optional.hpp>
#include <soci/soci.h>

#include <string>
#include <unordered_map>

namespace ripple {

class DatabaseCon;

// Value type for reservations.
struct PeerReservation
{
public:
    PublicKey nodeId;
    boost::optional<std::string> description;

    auto
    toJson() const -> Json::Value;
};

class PeerReservationTable
{
public:
    using table_type =
        std::unordered_map<PublicKey, PeerReservation, beast::uhash<>>;
    using const_iterator = table_type::const_iterator;

    explicit PeerReservationTable(
        beast::Journal journal = beast::Journal(beast::Journal::getNullSink()))
        : journal_(journal)
    {
    }

    const_iterator
    begin() const noexcept
    {
        return table_.begin();
    }

    const_iterator
    cbegin() const noexcept
    {
        return table_.cbegin();
    }

    const_iterator
    end() const noexcept
    {
        return table_.end();
    }

    const_iterator
    cend() const noexcept
    {
        return table_.cend();
    }

    bool
    contains(PublicKey const& nodeId)
    {
        return table_.find(nodeId) != table_.end();
    }

    // Because `ApplicationImp` has two-phase initialization, so must we.
    // Our dependencies are not prepared until the second phase.
    bool
    load(DatabaseCon& connection);

    /**
     * @return the replaced reservation if it existed
     * @throw soci::soci_error
     */
    auto
    upsert(PublicKey const& nodeId, boost::optional<std::string> const& desc)
        -> boost::optional<PeerReservation>;

    /**
     * @return the erased reservation if it existed
     */
    auto
    erase(PublicKey const& nodeId) -> boost::optional<PeerReservation>;

private:
    beast::Journal mutable journal_;
    DatabaseCon* connection_;
    table_type table_;
};

}  // namespace ripple

#endif
