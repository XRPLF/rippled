#ifndef XRPL_NET_RPCERR_H_INCLUDED
#define XRPL_NET_RPCERR_H_INCLUDED

#include <xrpl/json/json_value.h>

namespace ripple {

// VFALCO NOTE these are deprecated
bool
isRpcError(Json::Value jvResult);
Json::Value
rpcError(int iError);

}  // namespace ripple

#endif
