#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

// Forward declaration
class DepositPreauthBuilder;

/**
 * Ledger Entry: DepositPreauth
 * Type: ltDEPOSIT_PREAUTH (0x0070)
 * RPC Name: deposit_preauth
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use DepositPreauthBuilder to construct new ledger entries.
 */
class DepositPreauth : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltDEPOSIT_PREAUTH;

    /**
     * Construct a DepositPreauth ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit DepositPreauth(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for DepositPreauth");
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
     * Get sfAuthorize (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAuthorize() const
    {
        if (hasAuthorize())
            return this->sle_.at(sfAuthorize);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuthorize() const
    {
        return this->sle_.isFieldPresent(sfAuthorize);
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

    /**
     * Get sfAuthorizeCredentials (soeOPTIONAL)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuthorizeCredentials() const
    {
        if (this->sle_.isFieldPresent(sfAuthorizeCredentials))
            return this->sle_.getFieldArray(sfAuthorizeCredentials);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuthorizeCredentials() const
    {
        return this->sle_.isFieldPresent(sfAuthorizeCredentials);
    }
};

/**
 * Builder for DepositPreauth ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DepositPreauthBuilder : public LedgerEntryBuilderBase<DepositPreauthBuilder>
{
public:
    DepositPreauthBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<DepositPreauthBuilder>(ltDEPOSIT_PREAUTH)
    {
        setAccount(account);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    DepositPreauthBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltDEPOSIT_PREAUTH)
        {
            throw std::runtime_error("Invalid ledger entry type for DepositPreauth");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfAuthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfAuthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAuthorizeCredentials, value);
        return *this;
    }

    /**
     * Build and return the completed DepositPreauth wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    DepositPreauth
    build(uint256 const& index)
    {
        return DepositPreauth{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries
