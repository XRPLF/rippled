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

#ifndef RIPPLE_OVERLAY_TRAFFIC_H_INCLUDED
#define RIPPLE_OVERLAY_TRAFFIC_H_INCLUDED

#include "ripple.pb.h"

#include <atomic>
#include <map>

namespace ripple {

class TrafficCount
{

public:
    using Count_t = std::pair<
        std::atomic <unsigned long>,
        std::atomic <unsigned long> >;

    enum class category
    {
        CT_base,           // basic peer overhead, must be first
        CT_overlay,        // overlay management
        CT_transaction,
        CT_proposal,
        CT_validation,
        CT_get_ledger,     // ledgers we try to get
        CT_share_ledger,   // ledgers we share
        CT_get_trans,      // transaction sets we try to get
        CT_share_trans,    // transaction sets we get
        CT_unknown         // must be last
    };

    static const char* getName (category c);

    static category categorize (
        ::google::protobuf::Message const& message,
        int type, bool inbound);

    void addCount (category cat, bool inbound, int number)
    {
        if (inbound)
            counts_[cat].first += number;
        else
            counts_[cat].second += number;
    }

    TrafficCount()
    {
        for (category i = category::CT_base;
            i <= category::CT_unknown;
            i = static_cast<category>(static_cast<int>(i) + 1))
        {
            counts_[i].first.store (0l);
            counts_[i].second.store (0l);
        }
    }

    std::map <std::string, std::pair <unsigned long, unsigned long>>
    getCounts () const
    {
        std::map <std::string,
            std::pair <unsigned long, unsigned long>>
                ret;

        for (auto& i : counts_)
        {
            ret.emplace (getName (i.first),
                std::make_pair (
                    i.second.first.load(),
                    i.second.second.load()));
        }

        return ret;
    }

    protected:

    std::map <category, Count_t> counts_;
};

}
#endif
