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

class VaultDeleteBuilder;

/**
 * @brief Transaction: VaultDelete
 *
 * Type: ttVAULT_DELETE (67)
 * Delegable: Delegation::notDelegable
 * Amendment: featureSingleAssetVault
 * Privileges: mustDeleteAcct | destroyMPTIssuance | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultDeleteBuilder to construct new transactions.
 */
class VaultDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_DELETE;

    /**
     * @brief Construct a VaultDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultDelete");
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
};

/**
 * @brief Builder for VaultDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultDeleteBuilder : public TransactionBuilderBase<VaultDeleteBuilder>
{
public:
    /**
     * @brief Construct a new VaultDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultDeleteBuilder>(ttVAULT_DELETE, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    /**
     * @brief Construct a VaultDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for VaultDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultDeleteBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
