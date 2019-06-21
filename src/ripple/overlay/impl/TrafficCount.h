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

#include <ripple/basics/safe_cast.h>
#include <ripple/protocol/messages.h>

#include <atomic>
#include <cstdint>
#include <map>

namespace ripple {

class TrafficCount
{
public:
    class TrafficStats
    {
        public:

        std::atomic<std::uint64_t> bytesIn {0};
        std::atomic<std::uint64_t> bytesOut {0};
        std::atomic<std::uint64_t> messagesIn {0};
        std::atomic<std::uint64_t> messagesOut {0};

        TrafficStats() = default;

        TrafficStats(const TrafficStats& ts)
            : bytesIn (ts.bytesIn.load())
            , bytesOut (ts.bytesOut.load())
            , messagesIn (ts.messagesIn.load())
            , messagesOut (ts.messagesOut.load())
        {
        }

        operator bool () const
        {
            return messagesIn || messagesOut;
        }
    };


    enum class category
    {
        base,           // basic peer overhead, must be first

        cluster,        // cluster overhead
        overlay,        // overlay management
        manifests,      // manifest management
        transaction,
        proposal,
        validation,
        shards,         // shard-related traffic

        // TMHaveSet message:
        get_set,        // transaction sets we try to get
        share_set,      // transaction sets we get

        // TMLedgerData: transaction set candidate
        ld_tsc_get,
        ld_tsc_share,

        // TMLedgerData: transaction node
        ld_txn_get,
        ld_txn_share,

        // TMLedgerData: account state node
        ld_asn_get,
        ld_asn_share,

        // TMLedgerData: generic
        ld_get,
        ld_share,

        // TMGetLedger: transaction set candidate
        gl_tsc_share,
        gl_tsc_get,

        // TMGetLedger: transaction node
        gl_txn_share,
        gl_txn_get,

        // TMGetLedger: account state node
        gl_asn_share,
        gl_asn_get,

        // TMGetLedger: generic
        gl_share,
        gl_get,

        // TMGetObjectByHash:
        share_hash_ledger,
        get_hash_ledger,

        // TMGetObjectByHash:
        share_hash_tx,
        get_hash_tx,

        // TMGetObjectByHash: transaction node
        share_hash_txnode,
        get_hash_txnode,

        // TMGetObjectByHash: account state node
        share_hash_asnode,
        get_hash_asnode,

        // TMGetObjectByHash: CAS
        share_cas_object,
        get_cas_object,

        // TMGetObjectByHash: fetch packs
        share_fetch_pack,
        get_fetch_pack,

        // TMGetObjectByHash: generic
        share_hash,
        get_hash,

        unknown         // must be last
    };

    static const char* getName (category c);

    static category categorize (
        ::google::protobuf::Message const& message,
        int type, bool inbound);

    void addCount (category cat, bool inbound, int number)
    {
        if (inbound)
        {
            counts_[cat].bytesIn += number;
            ++counts_[cat].messagesIn;
        }
        else
        {
            counts_[cat].bytesOut += number;
            ++counts_[cat].messagesOut;
        }
    }

    TrafficCount()
    {
        for (category i = category::base;
            i <= category::unknown;
            i = safe_cast<category>(safe_cast<std::underlying_type_t<category>>(i) + 1))
        {
            counts_[i];
        }
    }

    std::map <std::string, TrafficStats>
    getCounts () const
    {
        std::map <std::string, TrafficStats> ret;

        for (auto& i : counts_)
        {
            if (i.second)
                ret.emplace (std::piecewise_construct,
                    std::forward_as_tuple (getName (i.first)),
                    std::forward_as_tuple (i.second));
        }

        return ret;
    }

protected:
    std::map<category, TrafficStats> counts_;
};

}
#endif
