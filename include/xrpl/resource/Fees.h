#ifndef XRPL_RESOURCE_FEES_H_INCLUDED
#define XRPL_RESOURCE_FEES_H_INCLUDED

#include <xrpl/resource/Charge.h>

namespace ripple {
namespace Resource {

// clang-format off
/** Schedule of fees charged for imposing load on the server. */
/** @{ */
extern Charge const feeMalformedRequest;    // A request that we can immediately
                                          //   tell is invalid
extern Charge const feeRequestNoReply;    // A request that we cannot satisfy
extern Charge const feeInvalidSignature;  // An object whose signature we had
                                          //   to check and it failed
extern Charge const feeUselessData;      // Data we have no use for
extern Charge const feeInvalidData;           // Data we have to verify before
                                          //   rejecting

// RPC loads
extern Charge const feeMalformedRPC;        // An RPC request that we can
                                          //   immediately tell is invalid.
extern Charge const feeReferenceRPC;      // A default "reference" unspecified
                                          //   load
extern Charge const feeExceptionRPC;      // RPC load that causes an exception
extern Charge const feeMediumBurdenRPC;   // A somewhat burdensome RPC load
extern Charge const feeHeavyBurdenRPC;     // A very burdensome RPC load

// Peer loads
extern Charge const feeTrivialPeer;         // Requires no reply
extern Charge const feeModerateBurdenPeer;  // Requires some work
extern Charge const feeHeavyBurdenPeer;    // Extensive work

// Administrative
extern Charge const feeWarning;           // The cost of receiving a warning
extern Charge const feeDrop;              // The cost of being dropped for
                                          //   excess load
/** @} */
// clang-format on

}  // namespace Resource
}  // namespace ripple

#endif
