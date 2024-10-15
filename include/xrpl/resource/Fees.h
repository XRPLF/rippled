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

#ifndef RIPPLE_RESOURCE_FEES_H_INCLUDED
#define RIPPLE_RESOURCE_FEES_H_INCLUDED

#include <xrpl/resource/Charge.h>

namespace ripple {
namespace Resource {

// clang-format off
/** Schedule of fees charged for imposing load on the server. */
/** @{ */
extern Charge const feeInvalidRequest;    // A request that we can immediately
                                          //   tell is invalid
extern Charge const feeRequestNoReply;    // A request that we cannot satisfy
extern Charge const feeInvalidSignature;  // An object whose signature we had
                                          //   to check and it failed
extern Charge const feeUnwantedData;      // Data we have no use for
extern Charge const feeBadData;           // Data we have to verify before
                                          //   rejecting

// RPC loads
extern Charge const feeInvalidRPC;        // An RPC request that we can
                                          //   immediately tell is invalid.
extern Charge const feeReferenceRPC;      // A default "reference" unspecified
                                          //   load
extern Charge const feeExceptionRPC;      // RPC load that causes an exception
extern Charge const feeMediumBurdenRPC;   // A somewhat burdensome RPC load
extern Charge const feeHighBurdenRPC;     // A very burdensome RPC load

// Peer loads
extern Charge const feeLightPeer;         // Requires no reply
extern Charge const feeMediumBurdenPeer;  // Requires some work
extern Charge const feeHighBurdenPeer;    // Extensive work

// Administrative
extern Charge const feeWarning;           // The cost of receiving a warning
extern Charge const feeDrop;              // The cost of being dropped for
                                          //   excess load
/** @} */
// clang-format on

}  // namespace Resource
}  // namespace ripple

#endif
