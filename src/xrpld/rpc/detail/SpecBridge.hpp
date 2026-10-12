#pragma once

#include <xrpld/rpc/detail/JsonObjectView.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/Types.hpp>

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace xrpl::rpc {

template <typename InputT>
using HandlerFor = ::rpc::spec::HandlerFor<InputT, json::Value>;

static_assert(
    std::variant_size_v<::rpc::CombinedError> == 1,
    "xrpl::rpc : an xrpld ::rpc::Status can hold only an ErrorCodeI");

inline void
injectSpecError(json::Value& object, ::rpc::Status const& status)
{
    if (auto const code = std::get<ErrorCodeI>(status.code); status.message.empty())
    {
        injectError(code, object);
    }
    else
    {
        injectError(code, status.message, object);
    }
}

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
            if (detail.empty())
                continue;
            message += ' ';
            message += detail;
        }

        json::Value& entry = array.append(json::Value{json::ValueType::Object});
        entry[jss::id] = static_cast<int>(code);
        entry[jss::message] = message;
    }
}

}  // namespace xrpl::rpc
