#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <stdexcept>

namespace xrpl {

/**
 * Read-only base class for all ledger entry view classes.
 *
 * Provides common functionality for existence checking and raw SLE read access.
 * Supports read-only (ReadView) contexts.
 *
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
class ReadOnlySLE
{
public:
    virtual ~ReadOnlySLE() = default;

    // Explicitly default copy/move operations
    ReadOnlySLE(ReadOnlySLE const&) = default;
    ReadOnlySLE(ReadOnlySLE&&) = default;
    ReadOnlySLE&
    operator=(ReadOnlySLE const&) = default;
    ReadOnlySLE&
    operator=(ReadOnlySLE&&) = default;

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

    /** Returns the read view (always available) */
    ReadView const*
    readView() const
    {
        return readView_;
    }

    STLedgerEntry const*
    operator->() const
    {
        XRPL_ASSERT(exists(), "xrpl::ReadOnlySLE::operator-> : exists");
        return sle_.get();
    }

    STLedgerEntry const&
    operator*() const
    {
        XRPL_ASSERT(exists(), "xrpl::ReadOnlySLE::operator* : exists");
        return *sle_;
    }

protected:
    ReadOnlySLE() = default;

    /** Constructor for read-only context (ReadView) */
    explicit ReadOnlySLE(std::shared_ptr<SLE const> sle, ReadView const* view)
        : sle_(std::move(sle)), readView_(view)
    {
    }

    std::shared_ptr<SLE const> sle_;  // Always valid (const view)
    ReadView const* readView_;        // Always valid
};

/**
 * Writable base class for all ledger entry view classes.
 *
 * Extends ReadOnlySLE with write access capabilities.
 * Supports read-write (ApplyView) contexts.
 *
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
class WritableSLE
{
public:
    virtual ~WritableSLE() = default;

    // Explicitly default copy/move operations
    WritableSLE(WritableSLE const&) = default;
    WritableSLE(WritableSLE&&) = default;
    WritableSLE&
    operator=(WritableSLE const&) = default;
    WritableSLE&
    operator=(WritableSLE&&) = default;

    /** Returns a mutable SLE for write operations */
    std::shared_ptr<SLE> const&
    mutableSle() const
    {
        return mutableSle_;
    }

    /** Returns true if this wrapper supports write operations */
    bool
    canModify() const
    {
        return applyView_ != nullptr && mutableSle_ != nullptr;
    }

    /** Returns the apply view for write operations */
    ApplyView*
    applyView() const
    {
        return applyView_;
    }

    STLedgerEntry*
    operator->()
    {
        XRPL_ASSERT(canModify(), "xrpl::WritableSLE::operator-> : can modify");
        return mutableSle_.get();
    }

    STLedgerEntry&
    operator*()
    {
        XRPL_ASSERT(canModify(), "xrpl::WritableSLE::operator* : can modify");
        return *mutableSle_;
    }

protected:
    WritableSLE() = default;

    /** Constructor for read-write context (ApplyView) */
    explicit WritableSLE(std::shared_ptr<SLE> sle, ApplyView* view)
        : mutableSle_(std::move(sle)), applyView_(view)
    {
    }

    std::shared_ptr<SLE> mutableSle_;  // Mutable SLE for write contexts
    ApplyView* applyView_;             // ApplyView for write contexts
};

}  // namespace xrpl
