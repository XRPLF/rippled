#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <stdexcept>
#include <utility>

namespace xrpl {

/**
 * @brief Before/after pair for one ledger entry touched by a transaction.
 *
 * Construction throws std::logic_error if after is null, or if isDelete is
 * true and before is null. visitEntry implementations may therefore
 * dereference after() unconditionally and before() whenever isDelete() is
 * true. before() is null for newly created entries. The entry holds shared
 * pointers to the SLEs and keeps them alive for its own lifetime. It is
 * passed by reference and is not copyable.
 */
class InvariantEntry
{
    bool isDelete_;
    SLE::const_pointer before_;
    SLE::const_pointer after_;

public:
    InvariantEntry(bool isDelete, SLE::const_pointer before, SLE::const_pointer after)
        : isDelete_(isDelete), before_(std::move(before)), after_(std::move(after))
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

    [[nodiscard]] SLE::ConstRef
    before() const
    {
        return before_;
    }

    [[nodiscard]] SLE::ConstRef
    after() const
    {
        return after_;
    }
};

}  // namespace xrpl
