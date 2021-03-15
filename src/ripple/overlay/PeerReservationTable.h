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

#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/json/json_forwards.h>
#include <ripple/protocol/PublicKey.h>

#define SOCI_USE_BOOST
#include <soci/soci.h>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace ripple {

class DatabaseCon;

// Value type for reservations.
struct PeerReservation final
{
public:
    PublicKey nodeId;
    std::string description{};

    auto
    toJson() const -> Json::Value;

    template <typename Hasher>
    friend void
    hash_append(Hasher& h, PeerReservation const& x) noexcept
    {
        using beast::hash_append;
        hash_append(h, x.nodeId);
    }

    friend bool
    operator<(PeerReservation const& a, PeerReservation const& b)
    {
        return a.nodeId < b.nodeId;
    }
};

// TODO: When C++20 arrives, take advantage of "equivalence" instead of
// "equality". Add an overload for `(PublicKey, PeerReservation)`, and just
// pass a `PublicKey` directly to `unordered_set.find`.
struct KeyEqual final
{
    bool
    operator()(PeerReservation const& lhs, PeerReservation const& rhs) const
    {
        return lhs.nodeId == rhs.nodeId;
    }
};

class PeerReservationTable final
{
public:
    explicit PeerReservationTable(
        beast::Journal journal = beast::Journal(beast::Journal::getNullSink()))
        : journal_(journal)
    {
    }

    std::vector<PeerReservation>
    list() const;

    bool
    contains(PublicKey const& nodeId)
    {
        std::lock_guard lock(this->mutex_);
        return table_.find({nodeId}) != table_.end();
    }

    // Because `ApplicationImp` has two-phase initialization, so must we.
    // Our dependencies are not prepared until the second phase.
    bool
    load(DatabaseCon& connection);

    /**
     * @return the replaced reservation if it existed
     * @throw soci::soci_error
     */
    std::optional<PeerReservation>
    insert_or_assign(PeerReservation const& reservation);

    /**
     * @return the erased reservation if it existed
     */
    std::optional<PeerReservation>
    erase(PublicKey const& nodeId);

private:
    beast::Journal mutable journal_;
    std::mutex mutable mutex_;
    DatabaseCon* connection_;
    std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual> table_;
};

}  // namespace ripple

#endif
