#include <test/jtx/pay.h>

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

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

}  // namespace jtx
}  // namespace test
}  // namespace ripple
