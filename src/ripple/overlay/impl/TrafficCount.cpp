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

#include <ripple/overlay/impl/TrafficCount.h>

namespace ripple {

const char* TrafficCount::getName (category c)
{
    switch (c)
    {
    case category::base:              return "overhead";
    case category::cluster:           return "cluster management";
    case category::overlay:           return "overlay management";
    case category::manifests:         return "manifest management";
    case category::transaction:       return "transactions";
    case category::proposal:          return "proposals management";
    case category::validation:        return "validations";
    case category::shards:            return "shards";
    case category::get_set:           return "set management (get)";
    case category::share_set:         return "set management(share)";
    case category::ld_tsc_get:        return "ledger data: Transaction Set candidate (get)";
    case category::ld_tsc_share:      return "ledger data: Transaction Set candidate (share)";
    case category::ld_txn_get:        return "ledger data: Transaction Node (get)";
    case category::ld_txn_share:      return "ledger data: Transaction Node (share)";
    case category::ld_asn_get:        return "ledger data: Account State Node (get)";
    case category::ld_asn_share:      return "ledger data: Account State Node (share)";
    case category::ld_get:            return "ledger data (get)";
    case category::ld_share:          return "ledger data (share)";
    case category::gl_tsc_share:      return "ledger: Transaction Set candidate (share)";
    case category::gl_tsc_get:        return "ledger: Transaction Set candidate (get)";
    case category::gl_txn_share:      return "ledger: Transaction node (share)";
    case category::gl_txn_get:        return "ledger: Transaction node (get)";
    case category::gl_asn_share:      return "ledger: Account State node (share)";
    case category::gl_asn_get:        return "ledger: Account State node (get)";
    case category::gl_share:          return "ledger (share)";
    case category::gl_get:            return "ledger (get)";
    case category::share_hash_ledger: return "getobject: Ledger (share)";
    case category::get_hash_ledger:   return "getobject: Ledger (get)";
    case category::share_hash_tx:     return "getobject: Transaction (share)";
    case category::get_hash_tx:       return "getobject: Transaction (get)";
    case category::share_hash_txnode: return "getobject: Transaction node (share)";
    case category::get_hash_txnode:   return "getobject: Transaction node (get)";
    case category::share_hash_asnode: return "getobject: Account State node (share)";
    case category::get_hash_asnode:   return "getobject: Account State node (get)";
    case category::share_cas_object:  return "getobject: CAS (share)";
    case category::get_cas_object:    return "getobject: CAS (get)";
    case category::share_fetch_pack:  return "getobject: Fetch Pack (share)";
    case category::get_fetch_pack:    return "getobject: Fetch Pack (get)";
    case category::share_hash:        return "getobject (share)";
    case category::get_hash:          return "getobject (get)";
    default:
    case category::unknown:           return "unknown";
    }
}

TrafficCount::category TrafficCount::categorize (
    ::google::protobuf::Message const& message,
    int type, bool inbound)
{
    if ((type == protocol::mtHELLO) ||
            (type == protocol::mtPING) ||
            (type == protocol::mtSTATUS_CHANGE))
        return TrafficCount::category::base;

    if (type == protocol::mtCLUSTER)
        return TrafficCount::category::cluster;

    if (type == protocol::mtMANIFESTS)
        return TrafficCount::category::manifests;

    if ((type == protocol::mtENDPOINTS) ||
            (type == protocol::mtPEERS) ||
            (type == protocol::mtGET_PEERS))
        return TrafficCount::category::overlay;

    if ((type == protocol::mtGET_SHARD_INFO) ||
            (type == protocol::mtSHARD_INFO) ||
            (type == protocol::mtGET_PEER_SHARD_INFO) ||
            (type == protocol::mtPEER_SHARD_INFO))
        return TrafficCount::category::shards;

    if (type == protocol::mtTRANSACTION)
        return TrafficCount::category::transaction;

    if (type == protocol::mtVALIDATION)
        return TrafficCount::category::validation;

    if (type == protocol::mtPROPOSE_LEDGER)
        return TrafficCount::category::proposal;

    if (type == protocol::mtHAVE_SET)
        return inbound ?
            TrafficCount::category::get_set:
            TrafficCount::category::share_set;

    if (auto msg = dynamic_cast<protocol::TMLedgerData const*>(&message))
    {
        if (msg->type() == protocol::liTS_CANDIDATE)
            return (inbound && !msg->has_requestcookie()) ?
                TrafficCount::category::ld_tsc_get:
                TrafficCount::category::ld_tsc_share;

        if (msg->type() == protocol::liTX_NODE)
            return (inbound && !msg->has_requestcookie()) ?
                TrafficCount::category::ld_txn_get :
                TrafficCount::category::ld_txn_share;

        if (msg->type() == protocol::liAS_NODE)
            return (inbound && !msg->has_requestcookie()) ?
                TrafficCount::category::ld_asn_get :
                TrafficCount::category::ld_asn_share;

        return (inbound && !msg->has_requestcookie()) ?
               TrafficCount::category::ld_get :
               TrafficCount::category::ld_share;
    }

    if (auto msg = dynamic_cast<protocol::TMGetLedger const*>(&message))
    {
        if (msg->itype() == protocol::liTS_CANDIDATE)
            return (inbound || msg->has_requestcookie()) ?
                TrafficCount::category::gl_tsc_share :
                TrafficCount::category::gl_tsc_get;

        if (msg->itype() == protocol::liTX_NODE)
            return (inbound || msg->has_requestcookie()) ?
                TrafficCount::category::gl_txn_share :
                TrafficCount::category::gl_txn_get;

        if (msg->itype() == protocol::liAS_NODE)
            return (inbound || msg->has_requestcookie()) ?
                TrafficCount::category::gl_asn_share :
                TrafficCount::category::gl_asn_get;

        return (inbound || msg->has_requestcookie()) ?
            TrafficCount::category::gl_share :
            TrafficCount::category::gl_get;
    }

    if (auto msg = dynamic_cast<protocol::TMGetObjectByHash const*>(&message))
    {
        if (msg->type() == protocol::TMGetObjectByHash::otLEDGER)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_hash_ledger :
                TrafficCount::category::get_hash_ledger;

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_hash_tx :
                TrafficCount::category::get_hash_tx;

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION_NODE)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_hash_txnode :
                TrafficCount::category::get_hash_txnode;

        if (msg->type() == protocol::TMGetObjectByHash::otSTATE_NODE)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_hash_asnode :
                TrafficCount::category::get_hash_asnode;

        if (msg->type() == protocol::TMGetObjectByHash::otCAS_OBJECT)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_cas_object :
                TrafficCount::category::get_cas_object;

        if (msg->type() == protocol::TMGetObjectByHash::otFETCH_PACK)
            return (msg->query() == inbound) ?
                TrafficCount::category::share_fetch_pack :
                TrafficCount::category::get_fetch_pack;

        return (msg->query() == inbound) ?
            TrafficCount::category::share_hash :
            TrafficCount::category::get_hash;
    }

    return TrafficCount::category::unknown;
}

} // ripple
