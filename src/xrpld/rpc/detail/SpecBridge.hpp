#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/Types.hpp>

#include <map>
#include <string>
#include <vector>

namespace xrpl::rpc {

// Warnings are grouped by code into one entry each: the code's standard message followed by
// every per-field detail, space separated.
//
// Entries already in the response are kept, so a handler may add its own.
inline void
injectSpecWarnings(json::Value& object, ::rpc::spec::Warnings const& warnings)
{
    if (warnings.empty())
        return;

    // Ordered, so the response does not depend on the order fields were visited in.
    std::map<::rpc::WarningCode, std::vector<std::string>> grouped;
    for (auto const& warning : warnings)
        grouped[warning.code].push_back(warning.message);

    json::Value& array = object.isMember(jss::warnings)
        ? object[jss::warnings]
        : (object[jss::warnings] = json::Value{json::ValueType::Array});

    for (auto const& [code, messages] : grouped)
    {
        std::string message{::rpc::getWarningInfo(code).message};
        for (auto const& detail : messages)
        {
            message += ' ';
            message += detail;
        }

        json::Value& entry = array.append(json::Value{json::ValueType::Object});
        entry[jss::id] = static_cast<int>(code);
        entry[jss::message] = message;
    }
}

}  // namespace xrpl::rpc
