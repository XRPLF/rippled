#ifndef XRPL_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED
#define XRPL_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/LedgerToJson.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/RippleLedgerHash.h>

namespace ripple {

/** Represents a ledger in RCLConsensus.

    RCLCxLedger is a thin wrapper over `std::shared_ptr<Ledger const>`.
*/
class RCLCxLedger
{
public:
    //! Unique identifier of a ledger
    using ID = LedgerHash;
    //! Sequence number of a ledger
    using Seq = LedgerIndex;

    /** Default constructor

        TODO: This may not be needed if we ensure RCLConsensus is handed a valid
        ledger in its constructor.  Its bad now because other members are not
        checking whether the ledger is valid.
    */
    RCLCxLedger() = default;

    /** Constructor

        @param l The ledger to wrap.
    */
    RCLCxLedger(std::shared_ptr<Ledger const> const& l) : ledger_{l}
    {
    }

    //! Sequence number of the ledger.
    Seq const&
    seq() const
    {
        return ledger_->info().seq;
    }

    //! Unique identifier (hash) of this ledger.
    ID const&
    id() const
    {
        return ledger_->info().hash;
    }

    //! Unique identifier (hash) of this ledger's parent.
    ID const&
    parentID() const
    {
        return ledger_->info().parentHash;
    }

    //! Resolution used when calculating this ledger's close time.
    NetClock::duration
    closeTimeResolution() const
    {
        return ledger_->info().closeTimeResolution;
    }

    //! Whether consensus process agreed on close time of the ledger.
    bool
    closeAgree() const
    {
        return ripple::getCloseAgree(ledger_->info());
    }

    //! The close time of this ledger
    NetClock::time_point
    closeTime() const
    {
        return ledger_->info().closeTime;
    }

    //! The close time of this ledger's parent.
    NetClock::time_point
    parentCloseTime() const
    {
        return ledger_->info().parentCloseTime;
    }

    //! JSON representation of this ledger.
    Json::Value
    getJson() const
    {
        return ripple::getJson({*ledger_, {}});
    }

    /** The ledger instance.

        TODO: Make this shared_ptr<ReadView const> .. requires ability to create
        a new ledger from a readView?
    */
    std::shared_ptr<Ledger const> ledger_;
};
}  // namespace ripple
#endif
