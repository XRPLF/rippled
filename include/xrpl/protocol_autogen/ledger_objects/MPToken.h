#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::ledger_entries {

// Forward declaration
class MPTokenBuilder;

/**
 * Ledger Entry: MPToken
 * Type: ltMPTOKEN (0x007f)
 * RPC Name: mptoken
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use MPTokenBuilder to construct new ledger entries.
 */
class MPToken : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltMPTOKEN;

    /**
     * Construct a MPToken ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit MPToken(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for MPToken");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_.at(sfAccount);
    }

    /**
     * Get sfMPTokenIssuanceID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->sle_.at(sfMPTokenIssuanceID);
    }

    /**
     * Get sfMPTAmount (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getMPTAmount() const
    {
        if (hasMPTAmount())
            return this->sle_.at(sfMPTAmount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMPTAmount() const
    {
        return this->sle_.isFieldPresent(sfMPTAmount);
    }

    /**
     * Get sfLockedAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getLockedAmount() const
    {
        if (hasLockedAmount())
            return this->sle_.at(sfLockedAmount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLockedAmount() const
    {
        return this->sle_.isFieldPresent(sfLockedAmount);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_.at(sfOwnerNode);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_.at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_.at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for MPToken ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class MPTokenBuilder : public LedgerEntryBuilderBase<MPTokenBuilder>
{
public:
    MPTokenBuilder(
                std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,
                std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,
                std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,
                std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,
                std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<MPTokenBuilder>(ltMPTOKEN)
    {
        setAccount(account);
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    MPTokenBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltMPTOKEN)
        {
            throw std::runtime_error("Invalid ledger entry type for MPToken");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * Set sfMPTAmount (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setMPTAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMPTAmount] = value;
        return *this;
    }

    /**
     * Set sfLockedAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setLockedAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLockedAmount] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed MPToken wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    MPToken
    build(uint256 const& index)
    {
        return MPToken{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries