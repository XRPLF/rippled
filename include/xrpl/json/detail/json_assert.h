#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>

#define JSON_ASSERT_MESSAGE(condition, message) \
    if (!(condition))                           \
        xrpl::Throw<Json::error>(message);
