#pragma once

#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>

namespace xrpl {

/**
 * Base class for all ledger entry view classes.
 *
 * Provides common functionality for existence checking and raw SLE access.
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
class LedgerEntryViewBase
{
public:
    /** Returns true if the ledger entry exists */
    bool
    exists() const
    {
        return sle_ != nullptr;
    }

    /** Explicit conversion to bool for convenient existence checking */
    explicit
    operator bool() const
    {
        return exists();
    }

    /** Returns the underlying SLE for raw access when needed */
    std::shared_ptr<SLE const> const&
    sle() const
    {
        return sle_;
    }

protected:
    LedgerEntryViewBase() = default;

    explicit LedgerEntryViewBase(std::shared_ptr<SLE const> sle) : sle_(std::move(sle))
    {
    }

    std::shared_ptr<SLE const> sle_;
};

}  // namespace xrpl
