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
class PayChannelBuilder;

/**
 * Ledger Entry: PayChannel
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
     * Construct a PayChannel ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PayChannel(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PayChannel");
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
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->sle_.at(sfDestination);
    }

    /**
     * Get sfSequence (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSequence() const
    {
        if (hasSequence())
            return this->sle_.at(sfSequence);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSequence() const
    {
        return this->sle_.isFieldPresent(sfSequence);
    }

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->sle_.at(sfAmount);
    }

    /**
     * Get sfBalance (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_.at(sfBalance);
    }

    /**
     * Get sfPublicKey (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->sle_.at(sfPublicKey);
    }

    /**
     * Get sfSettleDelay (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSettleDelay() const
    {
        return this->sle_.at(sfSettleDelay);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_.at(sfExpiration);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfCancelAfter (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
            return this->sle_.at(sfCancelAfter);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->sle_.isFieldPresent(sfCancelAfter);
    }

    /**
     * Get sfSourceTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSourceTag() const
    {
        if (hasSourceTag())
            return this->sle_.at(sfSourceTag);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSourceTag() const
    {
        return this->sle_.isFieldPresent(sfSourceTag);
    }

    /**
     * Get sfDestinationTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
            return this->sle_.at(sfDestinationTag);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->sle_.isFieldPresent(sfDestinationTag);
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
     * Get sfDestinationNode (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getDestinationNode() const
    {
        if (hasDestinationNode())
            return this->sle_.at(sfDestinationNode);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationNode() const
    {
        return this->sle_.isFieldPresent(sfDestinationNode);
    }
};

/**
 * Builder for PayChannel ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PayChannelBuilder : public LedgerEntryBuilderBase<PayChannelBuilder>
{
public:
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

    PayChannelBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltPAYCHAN)
        {
            throw std::runtime_error("Invalid ledger entry type for PayChannel");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfBalance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * Set sfSettleDelay (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSettleDelay(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleDelay] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfCancelAfter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * Set sfSourceTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setSourceTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSourceTag] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfDestinationNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PayChannelBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * Build and return the completed PayChannel wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    PayChannel
    build(uint256 const& index)
    {
        return PayChannel{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries
