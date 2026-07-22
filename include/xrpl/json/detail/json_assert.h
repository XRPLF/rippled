#pragma once

#define JSON_ASSERT_MESSAGE(condition, message) \
    if (!(condition))                           \
        xrpl::Throw<json::Error>(message);
