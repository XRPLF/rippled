// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

class FirewallSetBuilder;

/**
 * @brief Transaction: FirewallSet
 *
 * Type: ttFIREWALL_SET (104)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureFirewall
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use FirewallSetBuilder to construct new transactions.
 */
class FirewallSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttFIREWALL_SET;

    /**
     * @brief Construct a FirewallSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit FirewallSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for FirewallSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCounterparty (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getCounterparty() const
    {
        if (hasCounterparty())
        {
            return this->tx_->at(sfCounterparty);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterparty is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterparty() const
    {
        return this->tx_->isFieldPresent(sfCounterparty);
    }

    /**
     * @brief Get sfBackup (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getBackup() const
    {
        if (hasBackup())
        {
            return this->tx_->at(sfBackup);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBackup is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBackup() const
    {
        return this->tx_->isFieldPresent(sfBackup);
    }

    /**
     * @brief Get sfMaxFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMaxFee() const
    {
        if (hasMaxFee())
        {
            return this->tx_->at(sfMaxFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaxFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaxFee() const
    {
        return this->tx_->isFieldPresent(sfMaxFee);
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
        {
            return this->tx_->at(sfDestinationTag);
        }
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
        return this->tx_->isFieldPresent(sfDestinationTag);
    }
    /**
     * @brief Get sfCounterpartySignature (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getCounterpartySignature() const
    {
        if (this->tx_->isFieldPresent(sfCounterpartySignature))
            return this->tx_->getFieldObject(sfCounterpartySignature);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterpartySignature is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterpartySignature() const
    {
        return this->tx_->isFieldPresent(sfCounterpartySignature);
    }

    /**
     * @brief Get sfFirewallID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getFirewallID() const
    {
        if (hasFirewallID())
        {
            return this->tx_->at(sfFirewallID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFirewallID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFirewallID() const
    {
        return this->tx_->isFieldPresent(sfFirewallID);
    }
};

/**
 * @brief Builder for FirewallSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class FirewallSetBuilder : public TransactionBuilderBase<FirewallSetBuilder>
{
public:
    /**
     * @brief Construct a new FirewallSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    FirewallSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<FirewallSetBuilder>(ttFIREWALL_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a FirewallSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    FirewallSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttFIREWALL_SET)
        {
            throw std::runtime_error("Invalid transaction type for FirewallSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCounterparty (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfBackup (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setBackup(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBackup] = value;
        return *this;
    }

    /**
     * @brief Set sfMaxFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setMaxFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMaxFee] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterpartySignature (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Set sfFirewallID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallSetBuilder&
    setFirewallID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfFirewallID] = value;
        return *this;
    }

    /**
     * @brief Build and return the FirewallSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    FirewallSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return FirewallSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
