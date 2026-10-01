#pragma once

#include <xrpld/app/main/Application.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/MethodNames.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <string_view>

namespace xrpl::rpc {

class VersionHandler
{
public:
    explicit VersionHandler(JsonContext& c)
        : apiVersion_(c.apiVersion), betaEnabled_(c.app.config().betaRpcApi)
    {
    }

    static Status
    check()
    {
        return Status::kOK;
    }

    void
    writeResult(json::Value& obj) const
    {
        setVersion(obj, apiVersion_, betaEnabled_);
    }

    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr std::string_view name = method::kVersion;

    static constexpr unsigned minApiVer = rpc::kApiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = rpc::kApiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    unsigned int apiVersion_;
    bool betaEnabled_;
};

}  // namespace xrpl::rpc
