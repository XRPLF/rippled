#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/Output.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

void
httpReply(int nStatus, std::string const& strMsg, json::Output const&, beast::Journal j);

}  // namespace xrpl
