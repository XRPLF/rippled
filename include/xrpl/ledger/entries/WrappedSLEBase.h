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

    // Explicitly default copy/move operations
    WrappedSLEBase(WrappedSLEBase const&) = default;
    WrappedSLEBase(WrappedSLEBase&&) = default;
    WrappedSLEBase&
    operator=(WrappedSLEBase const&) = default;
    WrappedSLEBase&
    operator=(WrappedSLEBase&&) = default;

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

    /** Returns the underlying SLE for read access (always available) */
    std::shared_ptr<SLE const> const&
    sle() const
    {
        return sle_;
    }

    /** Returns a mutable SLE for write operations
     *
     * @throws std::logic_error if called in a read-only context
     */
    std::shared_ptr<SLE> const&
    mutableSle() const
    {
        if (!mutableSle_)
        {
            throw std::logic_error("Cannot modify SLE in read-only context");
        }
        return mutableSle_;
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

    STLedgerEntry*
    operator->()
    {
        return mutableSle_.get();
    }

    STLedgerEntry const*
    operator->() const
    {
        return sle_.get();
    }

    STLedgerEntry&
    operator*()
    {
        return *mutableSle_;
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
    explicit WrappedSLEBase(std::shared_ptr<SLE> sle, ApplyView* view)
        : sle_(sle), mutableSle_(std::move(sle)), readView_(view), applyView_(view)
    {
        // ApplyView inherits from ReadView, so we can use it for both
    }

    std::shared_ptr<SLE const> sle_;   // Always valid (const view)
    std::shared_ptr<SLE> mutableSle_;  // nullptr for read-only contexts
    ReadView const* readView_;         // Always valid
    ApplyView* applyView_;             // nullptr for read-only contexts
};

}  // namespace xrpl
