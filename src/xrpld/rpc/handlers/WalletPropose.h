#ifndef XRPL_XRPL_RPC_HANDLERS_WALLETPROPOSE_H
#define XRPL_XRPL_RPC_HANDLERS_WALLETPROPOSE_H

#include <xrpl/json/json_value.h>

namespace ripple {

Json::Value
walletPropose(Json::Value const& params);

}  // namespace ripple

#endif
