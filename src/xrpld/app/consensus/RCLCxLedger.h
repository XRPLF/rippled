#pragma once

#include <xrpld/app/ledger/LedgerToJson.h>

#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/RippleLedgerHash.h>

namespace xrpl {

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
    RCLCxLedger(std::shared_ptr<Ledger const> l) : ledger{std::move(l)}
    {
    }

    //! Sequence number of the ledger.
    [[nodiscard]] Seq const&
    seq() const
    {
        return ledger->header().seq;
    }

    //! Unique identifier (hash) of this ledger.
    [[nodiscard]] ID const&
    id() const
    {
        return ledger->header().hash;
    }

    //! Unique identifier (hash) of this ledger's parent.
    [[nodiscard]] ID const&
    parentID() const
    {
        return ledger->header().parentHash;
    }

    //! Resolution used when calculating this ledger's close time.
    [[nodiscard]] NetClock::duration
    closeTimeResolution() const
    {
        return ledger->header().closeTimeResolution;
    }

    //! Whether consensus process agreed on close time of the ledger.
    [[nodiscard]] bool
    closeAgree() const
    {
        return xrpl::getCloseAgree(ledger->header());
    }

    //! The close time of this ledger
    [[nodiscard]] NetClock::time_point
    closeTime() const
    {
        return ledger->header().closeTime;
    }

    //! The close time of this ledger's parent.
    [[nodiscard]] NetClock::time_point
    parentCloseTime() const
    {
        return ledger->header().parentCloseTime;
    }

    //! JSON representation of this ledger.
    [[nodiscard]] json::Value
    getJson() const
    {
        return xrpl::getJson({*ledger, {}});
    }

    /** The ledger instance.

        TODO: Make this shared_ptr<ReadView const> .. requires ability to create
        a new ledger from a readView?
    */
    std::shared_ptr<Ledger const> ledger;
};
}  // namespace xrpl
