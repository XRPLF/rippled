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

#ifndef RIPPLE_RESOURCE_LEGACYFEES_H_INCLUDED
#define RIPPLE_RESOURCE_LEGACYFEES_H_INCLUDED

namespace ripple {

enum LoadType
{
    // Bad things
    LT_InvalidRequest,          // A request that we can immediately tell is invalid
    LT_RequestNoReply,          // A request that we cannot satisfy
    LT_InvalidSignature,        // An object whose signature we had to check and it failed
    LT_UnwantedData,            // Data we have no use for
    LT_BadPoW,                  // Proof of work not valid
    LT_BadData,                 // Data we have to verify before rejecting

    // RPC loads
    LT_RPCInvalid,              // An RPC request that we can immediately tell is invalid.
    LT_RPCReference,            // A default "reference" unspecified load
    LT_RPCException,            // An RPC load that causes an exception
    LT_RPCBurden,               // A particularly burdensome RPC load

    // Good things
    LT_NewTrusted,              // A new transaction/validation/proposal we trust
    LT_NewTransaction,          // A new, valid transaction
    LT_NeededData,              // Data we requested

    // Requests
    LT_RequestData,             // A request that is hard to satisfy, disk access
    LT_CheapQuery,              // A query that is trivial, cached data

    LT_MAX                      // MUST BE LAST
};

}

#endif
