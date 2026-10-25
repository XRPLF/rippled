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

class VaultMarkBuilder;

/**
 * @brief Transaction: VaultMark
 *
 * Type: ttVAULT_MARK (122)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureManagedVault
 * Privileges: Privilege::MayModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultMarkBuilder to construct new transactions.
 */
class VaultMark : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_MARK;

    /**
     * @brief Construct a VaultMark transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultMark(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultMark");
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
     * @brief Get sfMarkPrice (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getMarkPrice() const
    {
        return this->tx_->at(sfMarkPrice);
    }
};

/**
 * @brief Builder for VaultMark transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultMarkBuilder : public TransactionBuilderBase<VaultMarkBuilder>
{
public:
    /**
     * @brief Construct a new VaultMarkBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param asset The sfAsset field value.
     * @param markPrice The sfMarkPrice field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultMarkBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_NUMBER::type::value_type> const& markPrice,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultMarkBuilder>(ttVAULT_MARK, account, sequence, fee)
    {
        setVaultID(vaultID);
        setAsset(asset);
        setMarkPrice(markPrice);
    }

    /**
     * @brief Construct a VaultMarkBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultMarkBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_MARK)
        {
            throw std::runtime_error("Invalid transaction type for VaultMarkBuilder");
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
    VaultMarkBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultMarkBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfMarkPrice (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultMarkBuilder&
    setMarkPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfMarkPrice] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultMark wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultMark
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultMark{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
