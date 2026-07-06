#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>

#include <xrpl/json/json_value.h>

#include <string>

namespace xrpl::RPC {

struct JsonContext;

/** Execute an RPC command and store the results in a json::Value. */
Status
doCommand(RPC::JsonContext&, json::Value&);

Role
roleRequired(unsigned int version, bool betaEnabled, std::string const& method);

}  // namespace xrpl::RPC
