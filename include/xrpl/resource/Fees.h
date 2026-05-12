#pragma once

#include <xrpl/resource/Charge.h>

namespace xrpl::Resource {

/** Schedule of fees charged for imposing load on the server. */
/** @{ */
extern Charge const kFEE_MALFORMED_REQUEST;  // A request that we can immediately tell is invalid.
extern Charge const kFEE_REQUEST_NO_REPLY;   // A request that we cannot satisfy.
extern Charge const
    kFEE_INVALID_SIGNATURE;             // An object whose signature we had to check that failed.
extern Charge const kFEE_USELESS_DATA;  // Data we have no use for.
extern Charge const kFEE_INVALID_DATA;  // Data we have to verify before rejecting.

// RPC loads
extern Charge const kFEE_MALFORMED_RPC;  // An RPC request that we can immediately tell is invalid.
extern Charge const kFEE_REFERENCE_RPC;  // A default "reference" unspecified load.
extern Charge const kFEE_EXCEPTION_RPC;  // RPC load that causes an exception.
extern Charge const kFEE_MEDIUM_BURDEN_RPC;  // A somewhat burdensome RPC load.
extern Charge const kFEE_HEAVY_BURDEN_RPC;   // A very burdensome RPC load.

// Peer loads
extern Charge const kFEE_TRIVIAL_PEER;          // Requires no reply.
extern Charge const kFEE_MODERATE_BURDEN_PEER;  // Requires some work.
extern Charge const kFEE_HEAVY_BURDEN_PEER;     // Extensive work.

// Administrative
extern Charge const kFEE_WARNING;  // The cost of receiving a warning.
extern Charge const kFEE_DROP;     // The cost of being dropped for excess load.
/** @} */

}  // namespace xrpl::Resource
