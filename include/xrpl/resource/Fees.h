#pragma once

#include <xrpl/resource/Charge.h>

namespace xrpl::Resource {

/** Schedule of fees charged for imposing load on the server. */
/** @{ */
extern Charge const kFeeMalformedRequest;  // A request that we can immediately tell is invalid.
extern Charge const kFeeRequestNoReply;    // A request that we cannot satisfy.
extern Charge const kFeeInvalidSignature;  // An object whose signature we had to check that failed.
extern Charge const kFeeUselessData;       // Data we have no use for.
extern Charge const kFeeInvalidData;       // Data we have to verify before rejecting.

// RPC loads
extern Charge const kFeeMalformedRpc;     // An RPC request that we can immediately tell is invalid.
extern Charge const kFeeReferenceRpc;     // A default "reference" unspecified load.
extern Charge const kFeeExceptionRpc;     // RPC load that causes an exception.
extern Charge const kFeeMediumBurdenRpc;  // A somewhat burdensome RPC load.
extern Charge const kFeeHeavyBurdenRpc;   // A very burdensome RPC load.

// Peer loads
extern Charge const kFeeTrivialPeer;         // Requires no reply.
extern Charge const kFeeModerateBurdenPeer;  // Requires some work.
extern Charge const kFeeHeavyBurdenPeer;     // Extensive work.

// Administrative
extern Charge const kFeeWarning;  // The cost of receiving a warning.
extern Charge const kFeeDrop;     // The cost of being dropped for excess load.
/** @} */

}  // namespace xrpl::Resource
