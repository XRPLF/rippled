#include <xrpl/resource/Fees.h>

#include <xrpl/resource/Charge.h>

namespace xrpl::Resource {

Charge const kFEE_MALFORMED_REQUEST(200, "malformed request");
Charge const kFEE_REQUEST_NO_REPLY(10, "unsatisfiable request");
Charge const kFEE_INVALID_SIGNATURE(2000, "invalid signature");
Charge const kFEE_USELESS_DATA(150, "useless data");
Charge const kFEE_INVALID_DATA(400, "invalid data");

Charge const kFEE_MALFORMED_RPC(100, "malformed RPC");
Charge const kFEE_REFERENCE_RPC(20, "reference RPC");
Charge const kFEE_EXCEPTION_RPC(100, "exceptioned RPC");
Charge const kFEE_MEDIUM_BURDEN_RPC(400, "medium RPC");
Charge const kFEE_HEAVY_BURDEN_RPC(3000, "heavy RPC");

Charge const kFEE_TRIVIAL_PEER(1, "trivial peer request");
Charge const kFEE_MODERATE_BURDEN_PEER(250, "moderate peer request");
Charge const kFEE_HEAVY_BURDEN_PEER(2000, "heavy peer request");

Charge const kFEE_WARNING(4000, "received warning");
Charge const kFEE_DROP(6000, "dropped");

// See also Resource::Logic::charge for log level cutoff values

}  // namespace xrpl::Resource
