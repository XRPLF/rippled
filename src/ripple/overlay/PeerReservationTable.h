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
#include <ripple/protocol/PublicKey.h>

#include <boost/optional.hpp>

#include <string>
#include <unordered_map>

namespace ripple {
// The rest of this directory is just in the `ripple` namespace.
// Do we want to stick with that pattern, or do we want the namespace to
// match the directory structure (to avoid name collisions and abide by
// the principle of least surprise)?
namespace overlay {

// Value type for reservations.
struct PeerReservation
{
public:
    PublicKey const identity_;
    boost::optional<std::string> name_;
};

using PeerReservationTable = std::unordered_map<PublicKey, PeerReservation, beast::uhash<>>;

}
}

#endif