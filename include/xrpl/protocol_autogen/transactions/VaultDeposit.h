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

class VaultDepositBuilder;

/**
 * @brief Transaction: VaultDeposit
 *
 * Type: ttVAULT_DEPOSIT (68)
 * Delegable: Delegation::notDelegable
 * Amendment: featureSingleAssetVault
 * Privileges: mayAuthorizeMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultDepositBuilder to construct new transactions.
 */
class VaultDeposit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_DEPOSIT;

    /**
     * @brief Construct a VaultDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultDeposit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultDeposit");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfVaultID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->tx_->at(sfVaultID);
    }

    /**
     * @brief Get sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }
};

/**
 * @brief Builder for VaultDeposit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultDepositBuilder : public TransactionBuilderBase<VaultDepositBuilder>
{
public:
    /**
     * @brief Construct a new VaultDepositBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultDepositBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultDepositBuilder>(ttVAULT_DEPOSIT, account, sequence, fee)
    {
        setVaultID(vaultID);
        setAmount(amount);
    }

    /**
     * @brief Construct a VaultDepositBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultDepositBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for VaultDepositBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultDepositBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultDeposit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultDeposit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultDeposit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
