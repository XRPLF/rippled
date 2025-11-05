#include <test/jtx/delegate.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace delegate {

Json::Value
set(jtx::Account const& account,
    jtx::Account const& authorize,
    std::vector<std::string> const& permissions)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DelegateSet;
    jv[jss::Account] = account.human();
    jv[sfAuthorize.jsonName] = authorize.human();
    Json::Value permissionsJson(Json::arrayValue);
    for (auto const& permission : permissions)
    {
        Json::Value permissionValue;
        permissionValue[sfPermissionValue.jsonName] = permission;
        Json::Value permissionObj;
        permissionObj[sfPermission.jsonName] = permissionValue;
        permissionsJson.append(permissionObj);
    }

    jv[sfPermissions.jsonName] = permissionsJson;

    return jv;
}

Json::Value
entry(jtx::Env& env, jtx::Account const& account, jtx::Account const& authorize)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::delegate][jss::account] = account.human();
    jvParams[jss::delegate][jss::authorize] = authorize.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace delegate
}  // namespace jtx
}  // namespace test
}  // namespace ripple
