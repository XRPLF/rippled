#ifndef XRPL_RPC_HANDLERS_GETCOUNTS_H_INCLUDED
#define XRPL_RPC_HANDLERS_GETCOUNTS_H_INCLUDED

#include <xrpld/app/main/Application.h>

namespace xrpl {

Json::Value
getCountsJson(Application& app, int minObjectCount);

}

#endif
