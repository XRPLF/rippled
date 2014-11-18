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

#ifndef RIPPLE_VALIDATORS_CONNECTIONIMP_H_INCLUDED
#define RIPPLE_VALIDATORS_CONNECTIONIMP_H_INCLUDED

#include <ripple/protocol/RippleAddress.h>
#include <ripple/validators/Connection.h>
#include <ripple/validators/impl/Logic.h>
#include <beast/container/hardened_hash.h>
#include <beast/utility/WrappedSink.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/optional.hpp>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include <map>

namespace ripple {
namespace Validators {

class ConnectionImp
    : public Connection
{
private:
    // Metadata on a validation source
    struct Source
    {
        // New sources are just at the threshold
        double score = 0.8;

        // returns `true` if the score is high
        // enough to count as available
        bool
        available() const
        {
            return score >= 0.8;
        }

        // returns `true` if the score is so low we
        // have no expectation of seeing the validator again
        bool
        gone() const
        {
            return score <= 0.2;
        }

        // returns `true` if became unavailable
        bool
        onHit()
        {
            bool const prev = available();
            score = 0.90 * score + 0.10;
            if (! prev && available())
                return true;
            return false;
        }

        // returns `true` if became available
        bool
        onMiss()
        {
            bool const prev = available();
            score = 0.90 * score;
            if (prev && ! available())
                return true;
            return false;
        }
    };

    using Item = std::pair<LedgerHash, RippleAddress>;

    Logic& logic_;
    beast::WrappedSink sink_;
    beast::Journal journal_;
    std::mutex mutex_;
    boost::optional<LedgerHash> ledger_;
    boost::container::flat_set<Item> items_;
    boost::container::flat_map<RippleAddress, Source> sources_;
    boost::container::flat_set<RippleAddress> good_;

    static
    std::string
    makePrefix (int id)
    {
        std::stringstream ss;
        ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
        return ss.str();
    }

public:
    template <class Clock>
    ConnectionImp (int id, Logic& logic, Clock& clock)
        : logic_ (logic)
        , sink_ (logic.journal(), makePrefix(id))
        , journal_ (sink_)
    {
        logic_.add(*this);
    }

    ~ConnectionImp()
    {
        logic_.remove(*this);
    }

    void
    onValidation (STValidation const& v) override
    {
        auto const key = v.getSignerPublic();
        auto const ledger = v.getLedgerHash();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (! items_.emplace(ledger, key).second)
                return;
            if (journal_.debug) journal_.debug <<
                "onValidation: " << ledger;
#if 0
            auto const result = sources_.emplace(
                std::piecewise_construct, std::make_tuple(key),
                    std::make_tuple());
#else
            // Work-around for boost::container
            auto const result = sources_.emplace(key, Source{});
#endif
            if (result.second)
                good_.insert(key);
            // register a hit for slightly late validations 
            if (ledger_ && ledger == ledger_)
                if (result.first->second.onHit())
                    good_.insert(key);
        }

        // This can call onLedger, do it last
        logic_.onValidation(v);
    }

    // Called when a supermajority of
    // validations are received for the next ledger.
    void
    onLedger (LedgerHash const& ledger)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (journal_.debug) journal_.debug <<
            "onLedger: " << ledger;
        assert(ledger != ledger_);
        ledger_ = ledger;
        auto item = items_.lower_bound(
            std::make_pair(ledger, RippleAddress()));
        auto source = sources_.begin();
        while (item != items_.end() &&
            source != sources_.end() &&
                item->first == ledger)
        {
            if (item->second < source->first)
            {
                ++item;
            }
            else if (item->second == source->first)
            {
                if (source->second.onHit())
                    good_.insert(source->first);
                ++item;
                ++source;
            }
            else
            {
                if (source->second.onMiss())
                    good_.erase(source->first);
                ++source;
            }
        }
        while (source != sources_.end())
        {
            if (source->second.onMiss())
                good_.erase(source->first);
            ++source;
        }
        // VFALCO What if there are validations
        // for the ledger AFTER this one in the map?
        items_.clear();
    }
};

}
}

#endif
