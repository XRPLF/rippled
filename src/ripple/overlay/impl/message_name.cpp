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

#ifndef RIPPLE_OVERLAY_MESSAGE_NAME_H_INCLUDED
#define RIPPLE_OVERLAY_MESSAGE_NAME_H_INCLUDED

namespace ripple {

char const*
protocol_message_name (int type)
{
    switch (type)
    {
    case protocol::mtHELLO:             return "hello";
    case protocol::mtPING:              return "ping";
    case protocol::mtPROOFOFWORK:       return "proof_of_work";
    case protocol::mtCLUSTER:           return "cluster";
    case protocol::mtGET_PEERS:         return "get_peers";
    case protocol::mtPEERS:             return "peers";
    case protocol::mtENDPOINTS:         return "endpoints";
    case protocol::mtTRANSACTION:       return "tx";
    case protocol::mtGET_LEDGER:        return "get_ledger";
    case protocol::mtLEDGER_DATA:       return "ledger_data";
    case protocol::mtPROPOSE_LEDGER:    return "propose";
    case protocol::mtSTATUS_CHANGE:     return "status";
    case protocol::mtHAVE_SET:          return "have_set";
    case protocol::mtVALIDATION:        return "validation";
    case protocol::mtGET_OBJECTS:       return "get_objects";
    default:
        break;
    };
    return "uknown";
}

}

#endif
