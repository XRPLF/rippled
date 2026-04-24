#include <test/jtx/regkey.h>

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

Json::Value
regkey(Account const& account, disabled_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::SetRegularKey;
    return jv;
}

Json::Value
regkey(Account const& account, Account const& signer)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv["RegularKey"] = to_string(signer.id());
    jv[jss::TransactionType] = jss::SetRegularKey;
    return jv;
}

}  // namespace xrpl::test::jtx
