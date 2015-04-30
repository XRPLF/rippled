//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_GETLEDGERTRACKER_H_INCLUDED
#define RIPPLE_OVERLAY_GETLEDGERTRACKER_H_INCLUDED

#include "ripple.pb.h"
#include <ripple/protocol/UintTypes.h>
#include <ripple/overlay/Message.h>
#include <beast/chrono/abstract_clock.h>
#include <beast/container/aged_container_utility.h>
#include <beast/container/aged_unordered_map.h>
#include <beast/hash/hash_append.h>
#include <beast/hash/uhash.h>
#include <beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <chrono>
#include <utility>

namespace ripple {

class GetLedgerTracker
{
private:
    using clock_type = std::chrono::high_resolution_clock;

    struct Value
    {
        boost::optional<std::uint32_t> id;
        clock_type::time_point when;
        std::size_t count;
        std::size_t bytes = 0;
    };

    using map_type = beast::aged_unordered_map<
        std::uint32_t, Value, std::chrono::steady_clock,
            beast::uhash<>>;
    
    beast::Journal j_;
    std::uint32_t next_id_ = 1;
    map_type map_;

public:
    explicit
    GetLedgerTracker (beast::Journal j);

    void
    onSend (protocol::TMGetLedger& m);

    void
    onReceive (protocol::TMGetLedger& m);

    void
    onSend (protocol::TMLedgerData& m);

    void
    onReceive (protocol::TMLedgerData& m);

    void
    onSend (protocol::TMGetObjectByHash const& m);

    void
    onReceive (protocol::TMGetObjectByHash const& m);

private:
    template <class Rep, class Period>
    std::string
    elapsed (std::chrono::duration<Rep, Period> const& d)
    {
        auto const ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(d).count();
        return std::to_string(ms) + "ms";
    }
};

}

#endif
