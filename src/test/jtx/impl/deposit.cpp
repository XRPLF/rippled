#include <test/jtx/deposit.h>

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <utility>
#include <vector>

namespace xrpl::test::jtx::deposit {

// Add DepositPreauth.
Json::Value
auth(jtx::Account const& account, jtx::Account const& auth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorize.jsonName] = auth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Remove DepositPreauth.
Json::Value
unauth(jtx::Account const& account, jtx::Account const& unauth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorize.jsonName] = unauth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Add DepositPreauth.
Json::Value
authCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorizeCredentials.jsonName] = Json::arrayValue;
    auto& arr(jv[sfAuthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        Json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Remove DepositPreauth.
Json::Value
unauthCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorizeCredentials.jsonName] = Json::arrayValue;
    auto& arr(jv[sfUnauthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        Json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

}  // namespace xrpl::test::jtx::deposit
