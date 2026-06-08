#pragma once

#include <xrpld/app/main/Application.h>

namespace xrpl {

json::Value
getCountsJson(Application& app, int minObjectCount);

}  // namespace xrpl
