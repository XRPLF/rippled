#ifndef XRPL_JSON_JSON_ASSERT_H_INCLUDED
#define XRPL_JSON_JSON_ASSERT_H_INCLUDED

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>

#define JSON_ASSERT_MESSAGE(condition, message) \
    if (!(condition))                           \
        ripple::Throw<Json::error>(message);

#endif
