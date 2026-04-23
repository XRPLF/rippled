#include <xrpld/app/ledger/LedgerReplay.h>

#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace xrpl {

LedgerReplay::LedgerReplay(
    std::shared_ptr<Ledger const> parent,
    std::shared_ptr<Ledger const> replay)
    : parent_{std::move(parent)}, replay_{std::move(replay)}
{
    for (auto const& item : replay_->txMap())
    {
        auto txPair = replay_->txRead(item.key());  // non-const so can be moved
        auto const txIndex = (*txPair.second)[sfTransactionIndex];
        orderedTxns_.emplace(txIndex, std::move(txPair.first));
    }
}

LedgerReplay::LedgerReplay(
    std::shared_ptr<Ledger const> parent,
    std::shared_ptr<Ledger const> replay,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
    : parent_{std::move(parent)}, replay_{std::move(replay)}, orderedTxns_{std::move(orderedTxns)}
{
}

}  // namespace xrpl
