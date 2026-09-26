#pragma once

#include <xrpld/app/main/Application.h>

#include <xrpl/json/json_value.h>

#include <cstdint>

namespace xrpl {

json::Value
getCountsJson(Application& app, std::uint32_t minObjectCount);

}  // namespace xrpl
