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

class VaultManagerClaimBuilder;

/**
 * @brief Transaction: VaultManagerClaim
 *
 * Type: ttVAULT_MANAGER_CLAIM (124)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureManagedVault
 * Privileges: Privilege::MayModifyVault | Privilege::MayAuthorizeMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultManagerClaimBuilder to construct new transactions.
 */
class VaultManagerClaim : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_MANAGER_CLAIM;

    /**
     * @brief Construct a VaultManagerClaim transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultManagerClaim(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultManagerClaim");
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
};

/**
 * @brief Builder for VaultManagerClaim transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultManagerClaimBuilder : public TransactionBuilderBase<VaultManagerClaimBuilder>
{
public:
    /**
     * @brief Construct a new VaultManagerClaimBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultManagerClaimBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultManagerClaimBuilder>(ttVAULT_MANAGER_CLAIM, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    /**
     * @brief Construct a VaultManagerClaimBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultManagerClaimBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_MANAGER_CLAIM)
        {
            throw std::runtime_error("Invalid transaction type for VaultManagerClaimBuilder");
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
    VaultManagerClaimBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultManagerClaim wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultManagerClaim
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultManagerClaim{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
