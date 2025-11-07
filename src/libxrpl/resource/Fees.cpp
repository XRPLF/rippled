#include <xrpl/resource/Charge.h>
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
