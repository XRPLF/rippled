#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

/** Deposit preauthorize operations */
namespace xrpl::test::jtx::deposit {

/** Preauthorize for deposit.  Invoke as deposit::auth. */
Json::Value
auth(Account const& account, Account const& auth);

/** Remove pre-authorization for deposit.  Invoke as deposit::unauth. */
Json::Value
unauth(Account const& account, Account const& unauth);

struct AuthorizeCredentials
{
    jtx::Account issuer;
    std::string credType;

    auto
    operator<=>(AuthorizeCredentials const&) const = default;

    [[nodiscard]] Json::Value
    toJson() const
    {
        Json::Value jv;
        jv[jss::Issuer] = issuer.human();
        jv[sfCredentialType.jsonName] = strHex(credType);
        return jv;
    }

    // "ledger_entry" uses a different naming convention
    [[nodiscard]] Json::Value
    toLEJson() const
    {
        Json::Value jv;
        jv[jss::issuer] = issuer.human();
        jv[jss::credential_type] = strHex(credType);
        return jv;
    }
};

Json::Value
authCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth);

Json::Value
unauthCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth);

}  // namespace xrpl::test::jtx::deposit
