#include <test/jtx/did.h>

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

/** DID operations. */
namespace did {

Json::Value
set(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    return jv;
}

Json::Value
setValid(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[sfURI.jsonName] = strHex(std::string{"uri"});
    return jv;
}

Json::Value
del(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDDelete;
    jv[jss::Account] = to_string(account.id());
    return jv;
}

}  // namespace did

}  // namespace jtx

}  // namespace test
}  // namespace ripple
