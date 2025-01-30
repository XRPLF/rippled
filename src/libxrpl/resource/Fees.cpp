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

#include <xrpl/resource/Fees.h>

namespace ripple {
namespace Resource {

Charge const feeMalformedRequest(200, "malformed request");
Charge const feeRequestNoReply(10, "unsatisfiable request");
Charge const feeInvalidSignature(2000, "invalid signature");
Charge const feeUselessData(150, "useless data");
Charge const feeInvalidData(400, "invalid data");

Charge const feeMalformedRPC(100, "malformed RPC");
Charge const feeReferenceRPC(20, "reference RPC");
Charge const feeExceptionRPC(100, "exceptioned RPC");
Charge const feeMediumBurdenRPC(400, "medium RPC");
Charge const feeHeavyBurdenRPC(3000, "heavy RPC");

Charge const feeTrivialPeer(1, "trivial peer request");
Charge const feeModerateBurdenPeer(250, "moderate peer request");
Charge const feeHeavyBurdenPeer(2000, "heavy peer request");

Charge const feeWarning(4000, "received warning");
Charge const feeDrop(6000, "dropped");

// See also Resource::Logic::charge for log level cutoff values

}  // namespace Resource
}  // namespace ripple
