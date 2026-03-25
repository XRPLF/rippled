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

class VaultClawbackBuilder;

/**
 * @brief Transaction: VaultClawback
 *
 * Type: ttVAULT_CLAWBACK (70)
 * Delegable: Delegation::notDelegable
 * Amendment: featureSingleAssetVault
 * Privileges: mayDeleteMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultClawbackBuilder to construct new transactions.
 */
class VaultClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_CLAWBACK;

    /**
     * @brief Construct a VaultClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultClawback");
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
     * @brief Get sfHolder (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_->at(sfHolder);
    }

    /**
     * @brief Get sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }
};

/**
 * @brief Builder for VaultClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultClawbackBuilder : public TransactionBuilderBase<VaultClawbackBuilder>
{
public:
    /**
     * @brief Construct a new VaultClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param holder The sfHolder field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultClawbackBuilder>(ttVAULT_CLAWBACK, account, sequence, fee)
    {
        setVaultID(vaultID);
        setHolder(holder);
    }

    /**
     * @brief Construct a VaultClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for VaultClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
