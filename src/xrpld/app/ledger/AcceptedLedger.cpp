#include <xrpld/app/ledger/AcceptedLedger.h>

#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/ReadView.h>

#include <algorithm>
#include <memory>

namespace xrpl {

AcceptedLedger::AcceptedLedger(std::shared_ptr<ReadView const> ledger) : ledger_(std::move(ledger))
{
    transactions_.reserve(256);

    auto insertAll = [&](auto const& txns) {
        for (auto const& item : txns)
        {
            transactions_.emplace_back(
                std::make_unique<AcceptedLedgerTx>(ledger_, item.first, item.second));
        }
    };

    transactions_.reserve(256);
    insertAll(ledger_->txs);

    std::ranges::sort(transactions_, [](auto const& a, auto const& b) {
        return a->getTxnSeq() < b->getTxnSeq();
    });
}

}  // namespace xrpl
