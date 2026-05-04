#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/contract.h>

#include <mutex>

namespace xrpl {

// Can std::atomic<std::shared_ptr>> make this lock free?

// VFALCO NOTE This class can be replaced with atomic<shared_ptr<...>>

/** Hold a ledger in a thread-safe way.

    VFALCO TODO The constructor should require a valid ledger, this
                way the object always holds a value. We can use the
                genesis ledger in all cases.
*/
class LedgerHolder : public CountedObject<LedgerHolder>
{
public:
    // Update the held ledger
    void
    set(std::shared_ptr<Ledger const> ledger)
    {
        if (!ledger)
            logicError("LedgerHolder::set with nullptr");
        if (!ledger->isImmutable())
            logicError("LedgerHolder::set with mutable Ledger");
        std::scoped_lock const sl(lock_);
        heldLedger_ = std::move(ledger);
    }

    // Return the (immutable) held ledger
    std::shared_ptr<Ledger const>
    get()
    {
        std::scoped_lock const sl(lock_);
        return heldLedger_;
    }

    bool
    empty()
    {
        std::scoped_lock const sl(lock_);
        return heldLedger_ == nullptr;
    }

private:
    std::mutex lock_;
    std::shared_ptr<Ledger const> heldLedger_;
};

}  // namespace xrpl
