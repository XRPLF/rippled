#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>

#include <rpcspec/Errors.hpp>

#include <string_view>

namespace xrpl::rpc {

struct JsonContext;

/**
 * Execute an RPC command and store the results in a json::Value.
 */
::rpc::Status
doCommand(rpc::JsonContext&, json::Value&);

Role
roleRequired(unsigned int version, bool betaEnabled, std::string_view method);

}  // namespace xrpl::rpc
