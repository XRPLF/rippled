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

#include <BeastConfig.h>
#include <ripple/overlay/impl/TrafficCount.h>

namespace ripple {

const char* TrafficCount::getName (Category c)
{
    switch (c)
    {
        case Category::CT_base:
            return "overhead";
        case Category::CT_overlay:
            return "overlay";
        case Category::CT_transaction:
            return "transactions";
        case Category::CT_proposal:
            return "proposals";
        case Category::CT_validation:
            return "validations";
        case Category::CT_get_ledger:
            return "ledger_get";
        case Category::CT_share_ledger:
            return "ledger_share";
        case Category::CT_get_trans:
            return "transaction_set_get";
        case Category::CT_share_trans:
            return "transaction_set_share";
        case Category::CT_unknown:
            assert (false);
            return "unknown";
        default:
            assert (false);
            return "truly_unknow";
    }
}

TrafficCount::Category TrafficCount::categorize (
    ::google::protobuf::Message const& message,
    int type, bool inbound)
{
    if ((type == protocol::mtHELLO) ||
            (type == protocol::mtPING) ||
            (type == protocol::mtCLUSTER) ||
            (type == protocol::mtSTATUS_CHANGE))
        return TrafficCount::Category::CT_base;

    if ((type == protocol::mtMANIFESTS) ||
            (type == protocol::mtENDPOINTS) ||
            (type == protocol::mtPEERS) ||
            (type == protocol::mtGET_PEERS))
        return TrafficCount::Category::CT_overlay;

    if (type == protocol::mtTRANSACTION)
        return TrafficCount::Category::CT_transaction;

    if (type == protocol::mtVALIDATION)
        return TrafficCount::Category::CT_validation;

    if (type == protocol::mtPROPOSE_LEDGER)
        return TrafficCount::Category::CT_proposal;

    if (type == protocol::mtHAVE_SET)
        return inbound ? TrafficCount::Category::CT_get_trans :
            TrafficCount::Category::CT_share_trans;

    {
        auto msg = dynamic_cast
            <protocol::TMLedgerData const*> (&message);
        if (msg)
        {
            // We have received ledger data
            if (msg->type() == protocol::liTS_CANDIDATE)
                return (inbound && !msg->has_requestcookie()) ?
                    TrafficCount::Category::CT_get_trans :
                    TrafficCount::Category::CT_share_trans;
            return (inbound && !msg->has_requestcookie()) ?
                TrafficCount::Category::CT_get_ledger :
                TrafficCount::Category::CT_share_ledger;
        }
    }

    {
        auto msg =
            dynamic_cast <protocol::TMGetLedger const*>
                (&message);
        if (msg)
        {
            if (msg->itype() == protocol::liTS_CANDIDATE)
                return (inbound || msg->has_requestcookie()) ?
                    TrafficCount::Category::CT_share_trans :
                    TrafficCount::Category::CT_get_trans;
            return (inbound || msg->has_requestcookie()) ?
                TrafficCount::Category::CT_share_ledger :
                TrafficCount::Category::CT_get_ledger;
        }
    }

    {
        auto msg =
            dynamic_cast <protocol::TMGetObjectByHash const*>
                (&message);
        if (msg)
        {
            // inbound queries and outbound responses are sharing
            // outbound queries and inbound responses are getting
            return (msg->query() == inbound) ?
                TrafficCount::Category::CT_share_ledger :
                TrafficCount::Category::CT_get_ledger;
        }
    }

    assert (false);
    return TrafficCount::Category::CT_unknown;
}

} // ripple
