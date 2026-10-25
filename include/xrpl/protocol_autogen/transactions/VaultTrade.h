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

class VaultTradeBuilder;

/**
 * @brief Transaction: VaultTrade
 *
 * Type: ttVAULT_TRADE (121)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureManagedVault
 * Privileges: Privilege::MustModifyVault | Privilege::MayAuthorizeMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultTradeBuilder to construct new transactions.
 */
class VaultTrade : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_TRADE;

    /**
     * @brief Construct a VaultTrade transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultTrade(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultTrade");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfVaultID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->tx_->at(sfVaultID);
    }

    /**
     * @brief Get sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * @brief Get sfSendMax (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSendMax() const
    {
        return this->tx_->at(sfSendMax);
    }

    /**
     * @brief Get sfDeliverMin (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getDeliverMin() const
    {
        if (hasDeliverMin())
        {
            return this->tx_->at(sfDeliverMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDeliverMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDeliverMin() const
    {
        return this->tx_->isFieldPresent(sfDeliverMin);
    }

    /**
     * @brief Get sfDomainID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_->at(sfDomainID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }
};

/**
 * @brief Builder for VaultTrade transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultTradeBuilder : public TransactionBuilderBase<VaultTradeBuilder>
{
public:
    /**
     * @brief Construct a new VaultTradeBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param amount The sfAmount field value.
     * @param sendMax The sfSendMax field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultTradeBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& sendMax,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultTradeBuilder>(ttVAULT_TRADE, account, sequence, fee)
    {
        setVaultID(vaultID);
        setAmount(amount);
        setSendMax(sendMax);
    }

    /**
     * @brief Construct a VaultTradeBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultTradeBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_TRADE)
        {
            throw std::runtime_error("Invalid transaction type for VaultTradeBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfVaultID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultTradeBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultTradeBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfSendMax (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultTradeBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * @brief Set sfDeliverMin (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultTradeBuilder&
    setDeliverMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfDeliverMin] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultTradeBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultTrade wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultTrade
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultTrade{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
