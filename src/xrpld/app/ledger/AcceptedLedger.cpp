#include <xrpld/app/ledger/AcceptedLedger.h>

#include <algorithm>

namespace ripple {

AcceptedLedger::AcceptedLedger(
    std::shared_ptr<ReadView const> const& ledger,
    Application& app)
    : mLedger(ledger)
{
    transactions_.reserve(256);

    auto insertAll = [&](auto const& txns) {
        for (auto const& item : txns)
            transactions_.emplace_back(std::make_unique<AcceptedLedgerTx>(
                ledger, item.first, item.second));
    };

    transactions_.reserve(256);
    insertAll(ledger->txs);

    std::sort(
        transactions_.begin(),
        transactions_.end(),
        [](auto const& a, auto const& b) {
            return a->getTxnSeq() < b->getTxnSeq();
        });
}

}  // namespace ripple
