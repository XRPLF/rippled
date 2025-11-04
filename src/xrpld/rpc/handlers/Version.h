#ifndef XRPL_XRPL_RPC_HANDLERS_VERSION_H
#define XRPL_XRPL_RPC_HANDLERS_VERSION_H

#include <xrpl/protocol/ApiVersion.h>

namespace ripple {
namespace RPC {

class VersionHandler
{
public:
    explicit VersionHandler(JsonContext& c)
        : apiVersion_(c.apiVersion), betaEnabled_(c.app.config().BETA_RPC_API)
    {
    }

    Status
    check()
    {
        return Status::OK;
    }

    template <class Object>
    void
    writeResult(Object& obj)
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

}  // namespace RPC
}  // namespace ripple

#endif
