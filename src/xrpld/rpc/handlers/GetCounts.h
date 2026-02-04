#pragma once

#include <xrpld/app/main/Application.h>

namespace xrpl {

Json::Value
getCountsJson(Application& app, int minObjectCount);

}
