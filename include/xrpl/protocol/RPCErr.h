#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>

namespace xrpl {

// VFALCO NOTE these are deprecated
bool
isRpcError(json::Value jvResult);
json::Value
rpcError(ErrorCodeI iError);

}  // namespace xrpl
