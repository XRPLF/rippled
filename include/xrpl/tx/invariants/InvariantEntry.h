#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <stdexcept>

namespace xrpl {

/**
 * A validated ledger entry visited by invariants.
 */
class InvariantEntry
{
    bool isDelete_;
    SLE::const_pointer before_;
    SLE::const_pointer after_;

public:
    InvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
        : isDelete_(isDelete), before_(before), after_(after)
    {
        if (after_ == nullptr)
            Throw<std::logic_error>("InvariantEntry: after is never null");
        if (isDelete_ && before_ == nullptr)
            Throw<std::logic_error>("InvariantEntry: deleted entry missing before state");
    }

    InvariantEntry(InvariantEntry const&) = delete;
    InvariantEntry&
    operator=(InvariantEntry const&) = delete;

    [[nodiscard]] bool
    isDelete() const
    {
        return isDelete_;
    }

    [[nodiscard]] SLE::const_ref
    before() const
    {
        return before_;
    }

    [[nodiscard]] SLE::const_ref
    after() const
    {
        return after_;
    }
};

}  // namespace xrpl
