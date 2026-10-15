#pragma once

#include <xrpld/app/main/Application.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

json::Value
getCountsJson(Application& app, int minObjectCount);

}  // namespace xrpl
