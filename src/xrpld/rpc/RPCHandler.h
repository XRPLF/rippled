#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>

namespace xrpl {
namespace RPC {

struct JsonContext;

/** Execute an RPC command and store the results in a Json::Value. */
Status
doCommand(RPC::JsonContext&, Json::Value&);

Role
roleRequired(unsigned int version, bool betaEnabled, std::string const& method);

}  // namespace RPC
}  // namespace xrpl
