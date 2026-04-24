#include <test/jtx/rate.h>

#include <test/jtx/Account.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <stdexcept>

namespace xrpl::test::jtx {

Json::Value
rate(Account const& account, double multiplier)
{
    if (multiplier > 4)
        Throw<std::runtime_error>("rate multiplier out of range");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransferRate] = std::uint32_t(1000000000 * multiplier);
    jv[jss::TransactionType] = jss::AccountSet;
    return jv;
}

}  // namespace xrpl::test::jtx
