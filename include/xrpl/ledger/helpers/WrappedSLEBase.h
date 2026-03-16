#pragma once

#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <stdexcept>

namespace xrpl {

class ReadView;
class ApplyView;

/**
 * Base class for all ledger entry view classes.
 *
 * Provides common functionality for existence checking and raw SLE access.
 * Supports both read-only (ReadView) and read-write (ApplyView) contexts.
 *
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
class WrappedSLEBase
{
public:
    virtual ~WrappedSLEBase() = default;

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

    /** Returns the read view (always available) */
    ReadView const*
    readView() const
    {
        return readView_;
    }

    /** Returns true if this wrapper supports write operations */
    bool
    canModify() const
    {
        return applyView_ != nullptr;
    }

    /** Returns the apply view for write operations
     *
     * @throws std::logic_error if called in a read-only context
     */
    ApplyView*
    applyView() const
    {
        if (!applyView_)
        {
            throw std::logic_error("Cannot access ApplyView in read-only context");
        }
        return applyView_;
    }

    STLedgerEntry const*
    operator->() const
    {
        return sle_.get();
    }

    STLedgerEntry const&
    operator*() const
    {
        return *sle_;
    }

protected:
    WrappedSLEBase() = default;

    /** Constructor for read-only context (ReadView) */
    explicit WrappedSLEBase(std::shared_ptr<SLE const> sle, ReadView const* view)
        : sle_(std::move(sle)), readView_(view), applyView_(nullptr)
    {
    }

    /** Constructor for read-write context (ApplyView) */
    explicit WrappedSLEBase(std::shared_ptr<SLE const> sle, ApplyView* view)
        : sle_(std::move(sle)), readView_(view), applyView_(view)
    {
        // ApplyView inherits from ReadView, so we can use it for both
    }

    std::shared_ptr<SLE const> sle_;
    ReadView const* readView_;
    ApplyView* applyView_;  // nullptr for read-only contexts
};

}  // namespace xrpl
