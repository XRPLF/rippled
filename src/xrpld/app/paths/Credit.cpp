#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>

namespace ripple {

STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    auto sleRippleState = view.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(
            account < issuer ? sfLowLimit : sfHighLimit);
        result.setIssuer(account);
    }

    XRPL_ASSERT(
        result.getIssuer() == account,
        "ripple::creditLimit : result issuer match");
    XRPL_ASSERT(
        result.getCurrency() == currency,
        "ripple::creditLimit : result currency match");
    return result;
}

IOUAmount
creditLimit2(
    ReadView const& v,
    AccountID const& acc,
    AccountID const& iss,
    Currency const& cur)
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
    STAmount result(Issue{currency, account});

    auto sleRippleState = view.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(sfBalance);
        if (account < issuer)
            result.negate();
        result.setIssuer(account);
    }

    XRPL_ASSERT(
        result.getIssuer() == account,
        "ripple::creditBalance : result issuer match");
    XRPL_ASSERT(
        result.getCurrency() == currency,
        "ripple::creditBalance : result currency match");
    return result;
}

}  // namespace ripple
