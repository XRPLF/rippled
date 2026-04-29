#include <test/jtx/regkey.h>

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

json::Value
regkey(Account const& account, DisabledT)
{
    json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::SetRegularKey;
    return jv;
}

json::Value
regkey(Account const& account, Account const& signer)
{
    json::Value jv;
    jv[jss::Account] = account.human();
    jv["RegularKey"] = to_string(signer.id());
    jv[jss::TransactionType] = jss::SetRegularKey;
    return jv;
}

}  // namespace xrpl::test::jtx
