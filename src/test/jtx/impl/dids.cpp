#include <test/jtx/Account.h>
#include <test/jtx/did.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

/** DID operations. */
namespace xrpl::test::jtx::did {

json::Value
set(jtx::Account const& account)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    return jv;
}

json::Value
setValid(jtx::Account const& account)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[sfURI.jsonName] = strHex(std::string{"uri"});
    return jv;
}

json::Value
del(jtx::Account const& account)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::DIDDelete;
    jv[jss::Account] = to_string(account.id());
    return jv;
}

}  // namespace xrpl::test::jtx::did
