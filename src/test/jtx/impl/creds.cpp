#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/credentials.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <string_view>

namespace xrpl::test::jtx::credentials {

json::Value
create(jtx::Account const& subject, jtx::Account const& issuer, std::string_view credType)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::CredentialCreate;

    jv[jss::Account] = issuer.human();
    jv[jss::Subject] = subject.human();
    jv[sfCredentialType.jsonName] = strHex(credType);

    return jv;
}

json::Value
accept(jtx::Account const& subject, jtx::Account const& issuer, std::string_view credType)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::CredentialAccept;
    jv[jss::Account] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    return jv;
}

json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::CredentialDelete;
    jv[jss::Account] = acc.human();
    jv[jss::Subject] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    return jv;
}

json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential][jss::subject] = subject.human();
    jvParams[jss::credential][jss::issuer] = issuer.human();
    jvParams[jss::credential][jss::credential_type] = strHex(credType);
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential] = credIdx;
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace xrpl::test::jtx::credentials
