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

class FirewallLimitSetBuilder;

/**
 * @brief Transaction: FirewallLimitSet
 *
 * Type: ttFIREWALL_LIMIT_SET (241)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureFirewallLimits
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use FirewallLimitSetBuilder to construct new transactions.
 */
class FirewallLimitSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttFIREWALL_LIMIT_SET;

    /**
     * @brief Construct a FirewallLimitSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit FirewallLimitSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for FirewallLimitSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_->at(sfAsset);
    }

    /**
     * @brief Get sfLimitAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLimitAmount() const
    {
        if (hasLimitAmount())
        {
            return this->tx_->at(sfLimitAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLimitAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLimitAmount() const
    {
        return this->tx_->isFieldPresent(sfLimitAmount);
    }

    /**
     * @brief Get sfPeriod (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPeriod() const
    {
        if (hasPeriod())
        {
            return this->tx_->at(sfPeriod);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPeriod is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPeriod() const
    {
        return this->tx_->isFieldPresent(sfPeriod);
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
};

/**
 * @brief Builder for FirewallLimitSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class FirewallLimitSetBuilder : public TransactionBuilderBase<FirewallLimitSetBuilder>
{
public:
    /**
     * @brief Construct a new FirewallLimitSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    FirewallLimitSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<FirewallLimitSetBuilder>(ttFIREWALL_LIMIT_SET, account, sequence, fee)
    {
        setAsset(asset);
    }

    /**
     * @brief Construct a FirewallLimitSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    FirewallLimitSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttFIREWALL_LIMIT_SET)
        {
            throw std::runtime_error("Invalid transaction type for FirewallLimitSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitSetBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfLimitAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitSetBuilder&
    setLimitAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLimitAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfPeriod (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitSetBuilder&
    setPeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterpartySignature (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitSetBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Build and return the FirewallLimitSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    FirewallLimitSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return FirewallLimitSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
