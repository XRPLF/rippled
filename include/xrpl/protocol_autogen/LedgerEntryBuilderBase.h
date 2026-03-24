#pragma once

#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol_autogen/STObjectValidation.h>

namespace xrpl::ledger_entries {

/**
 * Base class for all ledger entry builders.
 * Provides common field setters that are available for all ledger entry types.
 */
template <typename Derived>
class LedgerEntryBuilderBase
{
public:
    LedgerEntryBuilderBase() = default;

    LedgerEntryBuilderBase(
        SF_UINT16::type::value_type ledgerEntryType,
        SF_UINT32::type::value_type flags = 0)
    {
        // Don't call object_.set(soTemplate) - keep object_ as a free object.
        // This avoids creating STBase placeholders for soeDEFAULT fields,
        // which would cause applyTemplate() to throw "may not be explicitly
        // set to default" when building the SLE.
        // The SLE constructor will call applyTemplate() which properly
        // handles missing fields.
        object_[sfLedgerEntryType] = ledgerEntryType;
        object_[sfFlags] = flags;
    }

    /**
     * @brief Validate the ledger entry
     * @return true if validation passes, false otherwise
     */
    [[nodiscard]]
    bool
    validate() const
    {
        if (!object_.isFieldPresent(sfLedgerEntryType))
        {
            return false;  // LCOV_EXCL_LINE
        }
        auto ledgerEntryType = static_cast<LedgerEntryType>(object_.getFieldU16(sfLedgerEntryType));
        return protocol_autogen::validateSTObject(
            object_, LedgerFormats::getInstance().findByType(ledgerEntryType)->getSOTemplate());
    }

    /**
     * Set the ledger index.
     * @param value Ledger index
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setLedgerIndex(uint256 const& value)
    {
        object_[sfLedgerIndex] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the flags.
     * @param value Flags value
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setFlags(uint32_t value)
    {
        object_.setFieldU32(sfFlags, value);
        return static_cast<Derived&>(*this);
    }

protected:
    STObject object_{sfLedgerEntry};
};

}  // namespace xrpl::ledger_entries
