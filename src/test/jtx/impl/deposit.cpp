#include <test/jtx/deposit.h>

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <utility>
#include <vector>

namespace xrpl::test::jtx::deposit {

// Add DepositPreauth.
json::Value
auth(jtx::Account const& account, jtx::Account const& auth)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorize.jsonName] = auth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Remove DepositPreauth.
json::Value
unauth(jtx::Account const& account, jtx::Account const& unauth)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorize.jsonName] = unauth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Add DepositPreauth.
json::Value
authCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorizeCredentials.jsonName] = json::ArrayValue;
    auto& arr(jv[sfAuthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Remove DepositPreauth.
json::Value
unauthCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorizeCredentials.jsonName] = json::ArrayValue;
    auto& arr(jv[sfUnauthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

}  // namespace xrpl::test::jtx::deposit
