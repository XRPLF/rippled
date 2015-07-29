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

#ifndef RIPPLE_UNL_LOGIC_H_INCLUDED
#define RIPPLE_UNL_LOGIC_H_INCLUDED

#include <ripple/protocol/Protocol.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/unl/impl/Store.h>
#include <ripple/unl/impl/Tuning.h>
#include <beast/container/aged_container_utility.h>
#include <beast/container/aged_unordered_map.h>
#include <beast/container/aged_unordered_set.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/utility/Journal.h>
#include <boost/container/flat_set.hpp>
#include <memory>
#include <mutex>

namespace ripple {
namespace unl {

class BasicHorizon;

class Logic
{
private:
    struct LedgerMeta
    {
        std::uint32_t seq_no = 0;
        std::unordered_set<RippleAddress,
            hardened_hash<>> keys;
    };

    class Policy
    {
    public:
        /** Returns `true` if we should accept this as the last validated. */
        bool
        acceptLedgerMeta (std::pair<LedgerHash const, LedgerMeta> const& value)
        {
            return value.second.keys.size() >= 3; // quorum
        }
    };

    std::mutex mutex_;
    //Store& store_;
    beast::Journal journal_;

    Policy policy_;
    beast::aged_unordered_map <LedgerHash, LedgerMeta,
        std::chrono::steady_clock, hardened_hash<>> ledgers_;
    std::pair<LedgerHash, LedgerMeta> latest_; // last fully validated
    boost::container::flat_set<BasicHorizon*> connections_;

public:
    explicit
    Logic (Store& store, beast::Journal journal);

    beast::Journal const&
    journal() const
    {
        return journal_;
    }

    void stop();

    void load();

    void
    insert (BasicHorizon& c);

    void
    erase (BasicHorizon& c);

    bool
    isStale (STValidation const& v);

    void
    onTimer();

    void
    onMessage (protocol::TMValidation const& m,
        STValidation const& v);

    void
    onLedgerClosed (LedgerIndex index,
        LedgerHash const& hash, LedgerHash const& parent);
};

}
}

#endif
