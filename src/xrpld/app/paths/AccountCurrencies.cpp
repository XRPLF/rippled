#include <xrpld/app/paths/AccountCurrencies.h>

namespace ripple {

hash_set<Currency>
accountSourceCurrencies(
    AccountID const& account,
    std::shared_ptr<RippleLineCache> const& lrCache,
    bool includeXRP)
{
    hash_set<Currency> currencies;

    // YYY Only bother if they are above reserve
    if (includeXRP)
        currencies.insert(xrpCurrency());

    if (auto const lines =
            lrCache->getRippleLines(account, LineDirection::outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            // Filter out non
            if (saBalance > beast::zero
                // Have IOUs to send.
                ||
                (rspEntry.getLimitPeer()
                 // Peer extends credit.
                 && ((-saBalance) < rspEntry.getLimitPeer())))  // Credit left.
            {
                currencies.insert(saBalance.getCurrency());
            }
        }
    }

    currencies.erase(badCurrency());
    return currencies;
}

hash_set<Currency>
accountDestCurrencies(
    AccountID const& account,
    std::shared_ptr<RippleLineCache> const& lrCache,
    bool includeXRP)
{
    hash_set<Currency> currencies;

    if (includeXRP)
        currencies.insert(xrpCurrency());
    // Even if account doesn't exist

    if (auto const lines =
            lrCache->getRippleLines(account, LineDirection::outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            if (saBalance < rspEntry.getLimit())  // Can take more
                currencies.insert(saBalance.getCurrency());
        }
    }

    currencies.erase(badCurrency());
    return currencies;
}

}  // namespace ripple
