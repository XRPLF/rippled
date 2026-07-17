#include <xrpld/app/ledger/AcceptedLedger.h>

#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/ReadView.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace xrpl {

AcceptedLedger::AcceptedLedger(std::shared_ptr<ReadView const> ledger) : ledger_(std::move(ledger))
{
    transactions_.reserve(256);
    for (auto const& item : ledger_->txs)
    {
        transactions_.emplace_back(
            std::make_unique<AcceptedLedgerTx>(ledger_, item.first, item.second));
    }

    std::ranges::sort(transactions_, [](auto const& a, auto const& b) {
        return a->getTxnSeq() < b->getTxnSeq();
    });
}

}  // namespace xrpl
