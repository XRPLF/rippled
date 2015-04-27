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

#include <ripple/overlay/impl/GetLedgerTracker.h>
#include <cassert>

namespace ripple {

#ifndef RIPPLE_TRACK_GETLEDGER_TIMES
#define RIPPLE_TRACK_GETLEDGER_TIMES 0
#endif

#if RIPPLE_TRACK_GETLEDGER_TIMES
static bool trackGetLedger = true;
#else
static bool trackGetLedger = false;
#endif

GetLedgerTracker::GetLedgerTracker (beast::Journal j)
    : j_(j)
    , map_(beast::get_abstract_clock<
        std::chrono::steady_clock>())
{
}

void
GetLedgerTracker::onSend (protocol::TMGetLedger& m)
{
    if (! trackGetLedger)
        return;
    if (! m.has_requestcookie())
        return;
    beast::expire(map_,
        std::chrono::seconds(30));
    auto id = next_id_++;
    if (id == 0)
        id = next_id_++;
    auto const result =
        map_.emplace(id, std::move(Value{}));
    assert(result.second);
    auto& v = result.first->second;
    v.when = clock_type::now();
    if (m.has_requestcookie())
        v.id = m.requestcookie();
    v.count = m.nodeids().size();
    for (auto const& n : m.nodeids())
        v.bytes += n.size();
    m.set_requestcookie(id);
}

void
GetLedgerTracker::onReceive (protocol::TMGetLedger& m)
{
}

// TMLedgerData

void
GetLedgerTracker::onSend (protocol::TMLedgerData& m)
{
}

void
GetLedgerTracker::onReceive (protocol::TMLedgerData& m)
{
    if (! trackGetLedger)
        return;
    if (! m.has_requestcookie())
        return;

    beast::expire(map_,
        std::chrono::seconds(30));
    if (! m.has_requestcookie())
    {
        j_.error <<
            "TMLedgerData with no request cookie";
        return;
    }
    auto const id = m.requestcookie();
    auto const iter = map_.find(id);
    if (iter == map_.end())
    {
        j_.error <<
            "TMLedgerData with unknown request cookie";
        return;
    }
    auto const& v = iter->second;
    if (v.id)
        m.set_requestcookie(*v.id);
    else
        m.clear_requestcookie();
    auto const t = clock_type::now() - v.when;
    auto bytes = 0;
    for (auto const& n : m.nodes())
        bytes += n.nodedata().size();
    j_.info <<
        "seq=" << m.ledgerseq() <<
        ", in_count=" << v.count <<
        ", in_bytes=" << v.bytes <<
        ", count=" << m.nodes().size() <<
        ", bytes=" << bytes <<
        ", time=" << elapsed(t)
        ;
    map_.erase(iter);
}

void
GetLedgerTracker::onSend (
    protocol::TMGetObjectByHash const& m)
{
}

void
GetLedgerTracker::onReceive (
    protocol::TMGetObjectByHash const& m)
{
}

}
