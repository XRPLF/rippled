// This file is auto-generated. Do not edit.
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

class PayChannelBuilder;

/**
 * @brief Ledger Entry: PayChannel
 *
 * Type: ltPAYCHAN (0x0078)
 * RPC Name: payment_channel
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use PayChannelBuilder to construct new ledger entries.
 */
class PayChannel : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltPAYCHAN;

    /**
     * @brief Construct a PayChannel ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PayChannel(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PayChannel");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfDestination (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->sle_->at(sfDestination);
    }

    /**
     * @brief Get sfSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSequence() const
    {
        if (hasSequence())
            return this->sle_->at(sfSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSequence() const
    {
        return this->sle_->isFieldPresent(sfSequence);
    }

    /**
     * @brief Get sfAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->sle_->at(sfAmount);
    }

    /**
     * @brief Get sfBalance (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_->at(sfBalance);
    }

    /**
     * @brief Get sfPublicKey (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->sle_->at(sfPublicKey);
    }

    /**
     * @brief Get sfSettleDelay (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSettleDelay() const
    {
        return this->sle_->at(sfSettleDelay);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfCancelAfter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
            return this->sle_->at(sfCancelAfter);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCancelAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->sle_->isFieldPresent(sfCancelAfter);
    }

    /**
     * @brief Get sfSourceTag (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSourceTag() const
    {
        if (hasSourceTag())
            return this->sle_->at(sfSourceTag);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSourceTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSourceTag() const
    {
        return this->sle_->isFieldPresent(sfSourceTag);
    }

    /**
     * @brief Get sfDestinationTag (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
            return this->sle_->at(sfDestinationTag);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->sle_->isFieldPresent(sfDestinationTag);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }

    /**
     * @brief Get sfDestinationNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getDestinationNode() const
    {
        if (hasDestinationNode())
            return this->sle_->at(sfDestinationNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationNode() const
    {
        return this->sle_->isFieldPresent(sfDestinationNode);
    }
};

/**
 * @brief Builder for PayChannel ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PayChannelBuilder : public LedgerEntryBuilderBase<PayChannelBuilder>
{
public:
    /**
     * @brief Construct a new PayChannelBuilder with required fields.
     * @param account The sfAccount field value.
     * @param destination The sfDestination field value.
     * @param amount The sfAmount field value.
     * @param balance The sfBalance field value.
     * @param publicKey The sfPublicKey field value.
     * @param settleDelay The sfSettleDelay field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    PayChannelBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,std::decay_t<typename SF_AMOUNT::type::value_type> const& balance,std::decay_t<typename SF_VL::type::value_type> const& publicKey,std::decay_t<typename SF_UINT32::type::value_type> const& settleDelay,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<PayChannelBuilder>(ltPAYCHAN)
    {
        setAccount(account);
        setDestination(destination);
        setAmount(amount);
        setBalance(balance);
        setPublicKey(publicKey);
        setSettleDelay(settleDelay);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a PayChannelBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    PayChannelBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltPAYCHAN)
        {
            throw std::runtime_error("Invalid ledger entry type for PayChannel");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfBalance (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfPublicKey (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfSettleDelay (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSettleDelay(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleDelay] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfCancelAfter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfSourceTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSourceTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSourceTag] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed PayChannel wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    PayChannel
    build(uint256 const& index)
    {
        return PayChannel{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
