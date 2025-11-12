#include <test/jtx/credentials.h>

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace credentials {

Json::Value
create(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialCreate;

    jv[jss::Account] = issuer.human();
    jv[jss::Subject] = subject.human();
    jv[sfCredentialType.jsonName] = strHex(credType);

    return jv;
}

Json::Value
accept(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialAccept;
    jv[jss::Account] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    return jv;
}

Json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialDelete;
    jv[jss::Account] = acc.human();
    jv[jss::Subject] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    return jv;
}

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential][jss::subject] = subject.human();
    jvParams[jss::credential][jss::issuer] = issuer.human();
    jvParams[jss::credential][jss::credential_type] = strHex(credType);
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

Json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential] = credIdx;
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace credentials

}  // namespace jtx

}  // namespace test
}  // namespace ripple
