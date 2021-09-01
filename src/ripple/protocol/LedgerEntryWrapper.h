//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_LEDGER_ENTRY_WRAPPER_H_INCLUDED
#define RIPPLE_PROTOCOL_LEDGER_ENTRY_WRAPPER_H_INCLUDED

#include <ripple/basics/Blob.h>
#include <ripple/protocol/SField.h>
#include <type_traits>
#include <utility>

namespace ripple {

class STLedgerEntry;

// A base class for ledger entry wrappers.
//
// Provides basic management for:
//  o Handling of const/non-const representation
//  o The wrapped serialized ledger entry
//  o The flags
//  o Utility methods shared by derived classes
template <bool Writable>
class LedgerEntryWrapper
{
protected:
    using SleT = typename std::
        conditional_t<Writable, STLedgerEntry, STLedgerEntry const>;

    std::shared_ptr<SleT> wrapped_;

    //--------------------------------------------------------------------------
    // Constructors are protected so only derived classes can construct.
    LedgerEntryWrapper(std::shared_ptr<SleT>&& w) : wrapped_(std::move(w))
    {
    }

    LedgerEntryWrapper(LedgerEntryWrapper const&) = default;
    LedgerEntryWrapper(LedgerEntryWrapper&&) = default;
    LedgerEntryWrapper&
    operator=(LedgerEntryWrapper const&) = default;
    LedgerEntryWrapper&
    operator=(LedgerEntryWrapper&&) = default;

    // Destructor is protected so the base class cannot be directly destroyed.
    // There is no virtual table, so the most derived class must be destroyed.
    ~LedgerEntryWrapper() = default;

    //--------------------------------------------------------------------------
    // Helper functions that are useful to some derived classes.
    template <typename SF, typename T>
    void
    setOptional(SF const& field, T const& value)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOptional()requires an SField as its first argument.");

        if (!wrapped_->isFieldPresent(field))
            wrapped_->makeFieldPresent(field);

        wrapped_->at(field) = value;
    }

    template <typename SF>
    void
    clearOptional(SF const& field)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        static_assert(
            std::is_base_of_v<SField, SF>,
            "clearOptional()requires an SField as its argument.");

        if (wrapped_->isFieldPresent(field))
            wrapped_->makeFieldAbsent(field);
    }

    [[nodiscard]] Blob
    getOptionalVL(SF_VL const& field) const
    {
        Blob ret;
        if (wrapped_->isFieldPresent(field))
            ret = wrapped_->getFieldVL(field);
        return ret;
    }

    template <typename SF, std::size_t Bits, class Tag>
    void
    setOrClearBaseUintIfZero(SF const& field, base_uint<Bits, Tag> const& value)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOrClearBaseUintIfZero()requires an SField as its argument.");

        if (value.signum() == 0)
            return clearOptional(field);

        if (!wrapped_->isFieldPresent(field))
            wrapped_->makeFieldPresent(field);

        wrapped_->at(field) = value;
    }

    void
    setOrClearVLIfEmpty(SF_VL const& field, Blob const& value)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");

        if (value.empty())
            return clearOptional(field);

        if (!wrapped_->isFieldPresent(field))
            wrapped_->makeFieldPresent(field);

        wrapped_->setFieldVL(field, value);
    }

public:
    //--------------------------------------------------------------------------
    // Methods applicable to all ledger entries.

    [[nodiscard]] std::shared_ptr<STLedgerEntry const>
    slePtr() const
    {
        if constexpr (Writable)
            return std::const_pointer_cast<
                std::shared_ptr<STLedgerEntry const>>(wrapped_);
        else
            return wrapped_;
    }

    [[nodiscard]] std::shared_ptr<STLedgerEntry> const&
    slePtr()
    {
        static_assert(
            Writable, "Cannot access non-const SLE of const ledger entry.");
        return wrapped_;
    }

    [[nodiscard]] std::uint32_t
    flags() const
    {
        return wrapped_->at(sfFlags);
    }

    [[nodiscard]] bool
    isFlag(std::uint32_t flagsToCheck) const
    {
        return (flags() & flagsToCheck) == flagsToCheck;
    }

    void
    replaceAllFlags(std::uint32_t newFlags)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        wrapped_->at(sfFlags) = newFlags;
    }

    void
    setFlag(std::uint32_t flagsToSet)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        replaceAllFlags(flags() | flagsToSet);
    }

    void
    clearFlag(std::uint32_t flagsToClear)
    {
        static_assert(Writable, "Cannot set member of const ledger entry.");
        replaceAllFlags(flags() & ~flagsToClear);
    }
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_LEDGER_ENTRY_WRAPPER_H_INCLUDED
