#pragma once

#include <xrpl/protocol/ApiVersion.h>

namespace xrpl::RPC {

class VersionHandler
{
public:
    explicit VersionHandler(JsonContext& c)
        : apiVersion_(c.apiVersion), betaEnabled_(c.app.config().BETA_RPC_API)
    {
    }

    static Status
    check()
    {
        return Status::OK;
    }

    void
    writeResult(Json::Value& obj) const
    {
        setVersion(obj, apiVersion_, betaEnabled_);
    }

    static constexpr char const* name = "version";

    static constexpr unsigned minApiVer = RPC::apiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = RPC::apiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = NO_CONDITION;

private:
    unsigned int apiVersion_;
    bool betaEnabled_;
};

}  // namespace xrpl::RPC
