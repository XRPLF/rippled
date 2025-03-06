//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx/AccountPermission.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace account_permission {

Json::Value
accountPermissionSet(
    jtx::Account const& account,
    jtx::Account const& authorize,
    std::list<std::string> const& permissions)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::AccountPermissionSet;
    jv[jss::Account] = account.human();
    jv[jss::Authorize] = authorize.human();
    Json::Value permissionsJson(Json::arrayValue);
    for (auto const& permission : permissions)
    {
        Json::Value permissionValue;
        permissionValue[jss::PermissionValue] = permission;
        Json::Value permissionObj;
        permissionObj[jss::Permission] = permissionValue;
        permissionsJson.append(permissionObj);
    }

    jv[jss::Permissions] = permissionsJson;

    return jv;
}

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& account,
    jtx::Account const& authorize)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::account_permission][jss::account] = account.human();
    jvParams[jss::account_permission][jss::authorize] = authorize.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace account_permission
}  // namespace jtx
}  // namespace test
}  // namespace ripple
