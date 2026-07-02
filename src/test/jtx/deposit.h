#pragma once

#include <test/jtx/Account.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <string>
#include <vector>

/** Deposit preauthorize operations */
namespace xrpl::test::jtx::deposit {

/** Preauthorize for deposit.  Invoke as deposit::auth. */
json::Value
auth(Account const& account, Account const& auth);

/** Remove pre-authorization for deposit.  Invoke as deposit::unauth. */
json::Value
unauth(Account const& account, Account const& unauth);

struct AuthorizeCredentials
{
    jtx::Account issuer;
    std::string credType;

    auto
    operator<=>(AuthorizeCredentials const&) const = default;

    [[nodiscard]] json::Value
    toJson() const
    {
        json::Value jv;
        jv[jss::Issuer] = issuer.human();
        jv[sfCredentialType.jsonName] = strHex(credType);
        return jv;
    }

    // "ledger_entry" uses a different naming convention
    [[nodiscard]] json::Value
    toLEJson() const
    {
        json::Value jv;
        jv[jss::issuer] = issuer.human();
        jv[jss::credential_type] = strHex(credType);
        return jv;
    }
};

json::Value
authCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth);

json::Value
unauthCredentials(jtx::Account const& account, std::vector<AuthorizeCredentials> const& auth);

}  // namespace xrpl::test::jtx::deposit
