#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/RippleStateView.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>

namespace xrpl {

STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    auto result = RippleStateView::creditLimit(view, account, issuer, currency);
    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditLimit : result issuer match");
    XRPL_ASSERT(result.getCurrency() == currency, "xrpl::creditLimit : result currency match");
    return result;
}

IOUAmount
creditLimit2(ReadView const& v, AccountID const& acc, AccountID const& iss, Currency const& cur)
{
    return toAmount<IOUAmount>(creditLimit(v, acc, iss, cur));
}

STAmount
creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    auto result = RippleStateView::creditBalance(view, account, issuer, currency);
    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditBalance : result issuer match");
    XRPL_ASSERT(result.getCurrency() == currency, "xrpl::creditBalance : result currency match");
    return result;
}

}  // namespace xrpl
