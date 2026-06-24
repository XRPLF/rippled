#include <test/jtx/delegate.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <string>
#include <vector>

namespace xrpl::test::jtx::delegate {

json::Value
set(jtx::Account const& account,
    jtx::Account const& authorize,
    std::vector<std::string> const& permissions)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::DelegateSet;
    jv[jss::Account] = account.human();
    jv[sfAuthorize.jsonName] = authorize.human();
    json::Value permissionsJson(json::ValueType::Array);
    for (auto const& permission : permissions)
    {
        json::Value permissionValue;
        permissionValue[sfPermissionValue.jsonName] = permission;
        json::Value permissionObj;
        permissionObj[sfPermission.jsonName] = permissionValue;
        permissionsJson.append(permissionObj);
    }

    jv[sfPermissions.jsonName] = permissionsJson;

    return jv;
}

json::Value
entry(jtx::Env& env, jtx::Account const& account, jtx::Account const& authorize)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::delegate][jss::account] = account.human();
    jvParams[jss::delegate][jss::authorize] = authorize.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace xrpl::test::jtx::delegate
