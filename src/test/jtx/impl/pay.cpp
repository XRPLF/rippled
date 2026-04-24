#include <test/jtx/pay.h>

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

Json::Value
pay(AccountID const& account, AccountID const& to, AnyAmount amount)
{
    amount.to(to);
    Json::Value jv;
    jv[jss::Account] = to_string(account);
    jv[jss::Amount] = amount.value.getJson(JsonOptions::none);
    jv[jss::Destination] = to_string(to);
    jv[jss::TransactionType] = jss::Payment;
    jv[jss::Flags] = tfFullyCanonicalSig;
    return jv;
}
Json::Value
pay(Account const& account, Account const& to, AnyAmount amount)
{
    return pay(account.id(), to.id(), amount);
}

}  // namespace xrpl::test::jtx
