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

#include <array>
#include <atomic>
#include <cstdint>

namespace ripple {

class TrafficCount
{
public:
    class TrafficStats
    {
    public:
        std::string const name;

        std::atomic<std::uint64_t> bytesIn {0};
        std::atomic<std::uint64_t> bytesOut {0};
        std::atomic<std::uint64_t> messagesIn {0};
        std::atomic<std::uint64_t> messagesOut {0};

        TrafficStats(char const* n)
            : name (n)
        {
        }

        TrafficStats(TrafficStats const& ts)
            : name (ts.name)
            , bytesIn (ts.bytesIn.load())
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

    // If you add entries to this enum, you need to update the initialization
    // of the array at the bottom of this file which maps array numbers to
    // human-readable names.
    enum category : std::size_t
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

    /** Given a protocol message, determine which traffic category it belongs to */
    static category categorize (
        ::google::protobuf::Message const& message,
        int type, bool inbound);

    /** Account for traffic associated with the given category */
    void addCount (category cat, bool inbound, int bytes)
    {
        assert (cat <= category::unknown);

        if (inbound)
        {
            counts_[cat].bytesIn += bytes;
            ++counts_[cat].messagesIn;
        }
        else
        {
            counts_[cat].bytesOut += bytes;
            ++counts_[cat].messagesOut;
        }
    }

    TrafficCount() = default;

    /** An up-to-date copy of all the counters

        @return an object which satisfies the requirements of Container
     */
    auto
    getCounts () const
    {
        return counts_;
    }

protected:
    std::array<TrafficStats, category::unknown + 1> counts_
    {{
        { "overhead" },                                           // category::base
        { "overhead: cluster" },                                  // category::cluster
        { "overhead: overlay" },                                  // category::overlay
        { "overhead: manifest" },                                 // category::manifests
        { "transactions" },                                       // category::transaction
        { "proposals" },                                          // category::proposal
        { "validations" },                                        // category::validation
        { "shards" },                                             // category::shards
        { "set (get)" },                                          // category::get_set
        { "set (share)" },                                        // category::share_set
        { "ledger data: Transaction Set candidate (get)" },       // category::ld_tsc_get
        { "ledger data: Transaction Set candidate (share)" },     // category::ld_tsc_share
        { "ledger data: Transaction Node (get)" },                // category::ld_txn_get
        { "ledger data: Transaction Node (share)" },              // category::ld_txn_share
        { "ledger data: Account State Node (get)" },              // category::ld_asn_get
        { "ledger data: Account State Node (share)" },            // category::ld_asn_share
        { "ledger data (get)" },                                  // category::ld_get
        { "ledger data (share)" },                                // category::ld_share
        { "ledger: Transaction Set candidate (share)" },          // category::gl_tsc_share
        { "ledger: Transaction Set candidate (get)" },            // category::gl_tsc_get
        { "ledger: Transaction node (share)" },                   // category::gl_txn_share
        { "ledger: Transaction node (get)" },                     // category::gl_txn_get
        { "ledger: Account State node (share)" },                 // category::gl_asn_share
        { "ledger: Account State node (get)" },                   // category::gl_asn_get
        { "ledger (share)" },                                     // category::gl_share
        { "ledger (get)" },                                       // category::gl_get
        { "getobject: Ledger (share)" },                          // category::share_hash_ledger
        { "getobject: Ledger (get)" },                            // category::get_hash_ledger
        { "getobject: Transaction (share)" },                     // category::share_hash_tx
        { "getobject: Transaction (get)" },                       // category::get_hash_tx
        { "getobject: Transaction node (share)" },                // category::share_hash_txnode
        { "getobject: Transaction node (get)" },                  // category::get_hash_txnode
        { "getobject: Account State node (share)" },              // category::share_hash_asnode
        { "getobject: Account State node (get)" },                // category::get_hash_asnode
        { "getobject: CAS (share)" },                             // category::share_cas_object
        { "getobject: CAS (get)" },                               // category::get_cas_object
        { "getobject: Fetch Pack (share)" },                      // category::share_fetch_pack
        { "getobject: Fetch Pack (get)" },                        // category::get_fetch_pack
        { "getobject (share)" },                                  // category::share_hash
        { "getobject (get)" },                                    // category::get_hash
        { "unknown" }                                             // category::unknown
    }};
};

}
#endif
