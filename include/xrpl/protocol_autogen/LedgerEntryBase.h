#pragma once

#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/STObjectValidation.h>
#include <xrpl/protocol_autogen/Utils.h>

#include <optional>
#include <string>

namespace xrpl::ledger_entries {

/**
 * @brief Base class for type-safe ledger entry wrappers.
 *
 * This class provides common functionality for all ledger entry types,
 * including access to common fields (sfLedgerIndex, sfLedgerEntryType, sfFlags).
 *
 * This is an immutable wrapper around SLE (Serialized Ledger Entry).
 * Use the corresponding Builder classes to construct new ledger entries.
 */
class LedgerEntryBase
{
public:
    /**
     * @brief Construct a ledger entry wrapper from an existing SLE object.
     * @param sle The underlying serialized ledger entry to wrap
     */
    explicit LedgerEntryBase(SLE::const_pointer sle) : sle_(std::move(sle))
    {
    }

    /**
     * @brief Validate the ledger entry
     * @return true if validation passes, false otherwise
     */
    [[nodiscard]]
    bool
    validate() const
    {
        if (!sle_->isFieldPresent(sfLedgerEntryType))
        {
            return false;  // LCOV_EXCL_LINE
        }
        auto ledgerEntryType = static_cast<LedgerEntryType>(sle_->getFieldU16(sfLedgerEntryType));
        return protocol_autogen::validateSTObject(
            *sle_, LedgerFormats::getInstance().findByType(ledgerEntryType)->getSOTemplate());
    }

    /**
     * @brief Get the ledger entry type.
     * @return The type of this ledger entry
     */
    [[nodiscard]]
    LedgerEntryType
    getType() const
    {
        return sle_->getType();
    }

    /**
     * @brief Get the key (index) of this ledger entry.
     *
     * The key uniquely identifies this ledger entry in the ledger state.
     * @return A constant reference to the 256-bit key
     */
    [[nodiscard]]
    uint256 const&
    getKey() const
    {
        return sle_->key();
    }

    // Common field getters (from LedgerFormats.cpp commonFields)

    /**
     * @brief Get the ledger index (sfLedgerIndex).
     *
     * This field is OPTIONAL and represents the index of the ledger entry.
     * @return The ledger index if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint256>
    getLedgerIndex() const
    {
        if (sle_->isFieldPresent(sfLedgerIndex))
        {
            return sle_->at(sfLedgerIndex);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if the ledger entry has a ledger index.
     * @return true if sfLedgerIndex is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasLedgerIndex() const
    {
        return sle_->isFieldPresent(sfLedgerIndex);
    }

    /**
     * @brief Get the ledger entry type field (sfLedgerEntryType).
     *
     * This field is REQUIRED for all ledger entries and indicates the type
     * of the ledger entry (e.g., AccountRoot, RippleState, Offer, etc.).
     * @return The ledger entry type as a 16-bit unsigned integer
     */
    [[nodiscard]]
    uint16_t
    getLedgerEntryType() const
    {
        return sle_->at(sfLedgerEntryType);
    }

    /**
     * @brief Get the flags field (sfFlags).
     *
     * This field is REQUIRED for all ledger entries and contains
     * type-specific flags that modify the behavior of the ledger entry.
     * @return The flags value as a 32-bit unsigned integer
     */
    [[nodiscard]]
    std::uint32_t
    getFlags() const
    {
        return sle_->at(sfFlags);
    }

    /**
     * @brief Check if a specific flag is set.
     *
     * @param f The flag bitmask to check
     * @return true if all bits in f are set in the flags field
     */
    [[nodiscard]]
    bool
    isFlag(std::uint32_t f) const
    {
        return sle_->isFlag(f);
    }

    /**
     * @brief Get the underlying SLE object.
     *
     * Provides direct access to the wrapped serialized ledger entry object
     * for cases where the type-safe accessors are insufficient.
     * @return A constant reference to the underlying SLE object
     */
    [[nodiscard]]
    SLE::const_pointer
    getSle() const
    {
        return sle_;
    }

protected:
    /** @brief The underlying serialized ledger entry being wrapped. */
    SLE::const_pointer sle_;
};

}  // namespace xrpl::ledger_entries
