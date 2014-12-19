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

#ifndef RIPPLE_VALIDATORS_LOGIC_H_INCLUDED
#define RIPPLE_VALIDATORS_LOGIC_H_INCLUDED

#include <ripple/protocol/Protocol.h>
#include <ripple/basics/seconds_clock.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/validators/impl/Store.h>
#include <ripple/validators/impl/Tuning.h>
#include <beast/chrono/manual_clock.h>
#include <beast/container/aged_container_utility.h>
#include <beast/container/aged_unordered_map.h>
#include <beast/container/aged_unordered_set.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/utility/Journal.h>
#include <boost/container/flat_set.hpp>
#include <memory>
#include <mutex>

namespace ripple {
namespace Validators {

class ConnectionImp;

class Logic
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

private:
    struct LedgerMeta
    {
        std::uint32_t seq_no = 0;
        std::unordered_set<RippleAddress,
            beast::hardened_hash<>> keys;
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
        std::chrono::steady_clock, beast::hardened_hash<>> ledgers_;
    std::pair<LedgerHash, LedgerMeta> latest_; // last fully validated
    boost::container::flat_set<ConnectionImp*> connections_;
    
    //boost::container::flat_set<

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
    add (ConnectionImp& c);

    void
    remove (ConnectionImp& c);

    bool
    isStale (STValidation const& v);

    void
    onTimer();

    void
    onValidation (STValidation const& v);

    void
    onLedgerClosed (LedgerIndex index,
        LedgerHash const& hash, LedgerHash const& parent);
};

}
}

#endif
