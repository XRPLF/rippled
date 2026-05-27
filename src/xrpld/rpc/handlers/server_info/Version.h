#pragma once

#include <xrpl/protocol/ApiVersion.h>

namespace xrpl::RPC {

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
    static constexpr char const* name = "version";

    static constexpr unsigned minApiVer = RPC::kApiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = RPC::kApiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    unsigned int apiVersion_;
    bool betaEnabled_;
};

}  // namespace xrpl::RPC
