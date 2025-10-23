#ifndef XRPL_APP_LEDGER_LEDGERREPLAY_H_INCLUDED
#define XRPL_APP_LEDGER_LEDGERREPLAY_H_INCLUDED

#include <xrpl/basics/CountedObject.h>

#include <cstdint>
#include <map>

namespace ripple {

class Ledger;
class STTx;

class LedgerReplay : public CountedObject<LedgerReplay>
{
    std::shared_ptr<Ledger const> parent_;
    std::shared_ptr<Ledger const> replay_;
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns_;

public:
    LedgerReplay(
        std::shared_ptr<Ledger const> parent,
        std::shared_ptr<Ledger const> replay);

    LedgerReplay(
        std::shared_ptr<Ledger const> parent,
        std::shared_ptr<Ledger const> replay,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns);

    /** @return The parent of the ledger to replay
     */
    std::shared_ptr<Ledger const> const&
    parent() const
    {
        return parent_;
    }

    /** @return The ledger to replay
     */
    std::shared_ptr<Ledger const> const&
    replay() const
    {
        return replay_;
    }

    /** @return Transactions in the order they should be replayed
     */
    std::map<std::uint32_t, std::shared_ptr<STTx const>> const&
    orderedTxns() const
    {
        return orderedTxns_;
    }
};

}  // namespace ripple

#endif
