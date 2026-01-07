#ifndef XRPL_SERVER_JSONRPCUTIL_H_INCLUDED
#define XRPL_SERVER_JSONRPCUTIL_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/Output.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

void
HTTPReply(
    int nStatus,
    std::string const& strMsg,
    Json::Output const&,
    beast::Journal j);

}  // namespace xrpl

#endif
