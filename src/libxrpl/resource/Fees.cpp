#include <xrpl/resource/Fees.h>

#include <xrpl/resource/Charge.h>

namespace xrpl::Resource {

Charge const kFeeMalformedRequest(200, "malformed request");
Charge const kFeeRequestNoReply(10, "unsatisfiable request");
Charge const kFeeInvalidSignature(2000, "invalid signature");
Charge const kFeeUselessData(150, "useless data");
Charge const kFeeInvalidData(400, "invalid data");

Charge const kFeeMalformedRpc(100, "malformed RPC");
Charge const kFeeReferenceRpc(20, "reference RPC");
Charge const kFeeExceptionRpc(100, "exceptioned RPC");
Charge const kFeeMediumBurdenRpc(400, "medium RPC");
Charge const kFeeHeavyBurdenRpc(3000, "heavy RPC");

Charge const kFeeTrivialPeer(1, "trivial peer request");
Charge const kFeeModerateBurdenPeer(250, "moderate peer request");
Charge const kFeeHeavyBurdenPeer(2000, "heavy peer request");

Charge const kFeeWarning(4000, "received warning");
Charge const kFeeDrop(6000, "dropped");

// See also Resource::Logic::charge for log level cutoff values

}  // namespace xrpl::Resource
