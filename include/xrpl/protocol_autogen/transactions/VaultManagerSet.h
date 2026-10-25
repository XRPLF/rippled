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

class VaultManagerSetBuilder;

/**
 * @brief Transaction: VaultManagerSet
 *
 * Type: ttVAULT_MANAGER_SET (120)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureManagedVault
 * Privileges: Privilege::CreatePseudoAcct | Privilege::MayAuthorizeMpt | Privilege::MayModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultManagerSetBuilder to construct new transactions.
 */
class VaultManagerSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_MANAGER_SET;

    /**
     * @brief Construct a VaultManagerSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultManagerSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultManagerSet");
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
     * @brief Get sfManager (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getManager() const
    {
        if (hasManager())
        {
            return this->tx_->at(sfManager);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfManager is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasManager() const
    {
        return this->tx_->isFieldPresent(sfManager);
    }

    /**
     * @brief Get sfValuer (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getValuer() const
    {
        if (hasValuer())
        {
            return this->tx_->at(sfValuer);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfValuer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasValuer() const
    {
        return this->tx_->isFieldPresent(sfValuer);
    }

    /**
     * @brief Get sfGuardian (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getGuardian() const
    {
        if (hasGuardian())
        {
            return this->tx_->at(sfGuardian);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfGuardian is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasGuardian() const
    {
        return this->tx_->isFieldPresent(sfGuardian);
    }
    /**
     * @brief Get sfMandate (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getMandate() const
    {
        if (this->tx_->isFieldPresent(sfMandate))
            return this->tx_->getFieldArray(sfMandate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMandate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMandate() const
    {
        return this->tx_->isFieldPresent(sfMandate);
    }

    /**
     * @brief Get sfDeployedMaximum (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getDeployedMaximum() const
    {
        if (hasDeployedMaximum())
        {
            return this->tx_->at(sfDeployedMaximum);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDeployedMaximum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDeployedMaximum() const
    {
        return this->tx_->isFieldPresent(sfDeployedMaximum);
    }

    /**
     * @brief Get sfCoverRateMinimum (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateMinimum() const
    {
        if (hasCoverRateMinimum())
        {
            return this->tx_->at(sfCoverRateMinimum);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateMinimum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateMinimum() const
    {
        return this->tx_->isFieldPresent(sfCoverRateMinimum);
    }

    /**
     * @brief Get sfCoverRateLiquidation (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateLiquidation() const
    {
        if (hasCoverRateLiquidation())
        {
            return this->tx_->at(sfCoverRateLiquidation);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateLiquidation is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateLiquidation() const
    {
        return this->tx_->isFieldPresent(sfCoverRateLiquidation);
    }

    /**
     * @brief Get sfManagementFeeRate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getManagementFeeRate() const
    {
        if (hasManagementFeeRate())
        {
            return this->tx_->at(sfManagementFeeRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfManagementFeeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasManagementFeeRate() const
    {
        return this->tx_->isFieldPresent(sfManagementFeeRate);
    }

    /**
     * @brief Get sfPerformanceFeeRate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPerformanceFeeRate() const
    {
        if (hasPerformanceFeeRate())
        {
            return this->tx_->at(sfPerformanceFeeRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPerformanceFeeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPerformanceFeeRate() const
    {
        return this->tx_->isFieldPresent(sfPerformanceFeeRate);
    }

    /**
     * @brief Get sfMarkMoveLimit (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarkMoveLimit() const
    {
        if (hasMarkMoveLimit())
        {
            return this->tx_->at(sfMarkMoveLimit);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkMoveLimit is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkMoveLimit() const
    {
        return this->tx_->isFieldPresent(sfMarkMoveLimit);
    }

    /**
     * @brief Get sfMarkHaltPeriod (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarkHaltPeriod() const
    {
        if (hasMarkHaltPeriod())
        {
            return this->tx_->at(sfMarkHaltPeriod);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkHaltPeriod is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkHaltPeriod() const
    {
        return this->tx_->isFieldPresent(sfMarkHaltPeriod);
    }

    /**
     * @brief Get sfMarkStaleAfter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarkStaleAfter() const
    {
        if (hasMarkStaleAfter())
        {
            return this->tx_->at(sfMarkStaleAfter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkStaleAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkStaleAfter() const
    {
        return this->tx_->isFieldPresent(sfMarkStaleAfter);
    }

    /**
     * @brief Get sfTimelock (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTimelock() const
    {
        if (hasTimelock())
        {
            return this->tx_->at(sfTimelock);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTimelock is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTimelock() const
    {
        return this->tx_->isFieldPresent(sfTimelock);
    }
};

/**
 * @brief Builder for VaultManagerSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultManagerSetBuilder : public TransactionBuilderBase<VaultManagerSetBuilder>
{
public:
    /**
     * @brief Construct a new VaultManagerSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultManagerSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultManagerSetBuilder>(ttVAULT_MANAGER_SET, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    /**
     * @brief Construct a VaultManagerSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultManagerSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_MANAGER_SET)
        {
            throw std::runtime_error("Invalid transaction type for VaultManagerSetBuilder");
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
    VaultManagerSetBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfManager (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setManager(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfManager] = value;
        return *this;
    }

    /**
     * @brief Set sfValuer (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setValuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfValuer] = value;
        return *this;
    }

    /**
     * @brief Set sfGuardian (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setGuardian(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfGuardian] = value;
        return *this;
    }

    /**
     * @brief Set sfMandate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setMandate(STArray const& value)
    {
        object_.setFieldArray(sfMandate, value);
        return *this;
    }

    /**
     * @brief Set sfDeployedMaximum (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setDeployedMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDeployedMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateLiquidation (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setCoverRateLiquidation(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateLiquidation] = value;
        return *this;
    }

    /**
     * @brief Set sfManagementFeeRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setManagementFeeRate(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfManagementFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfPerformanceFeeRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setPerformanceFeeRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPerformanceFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkMoveLimit (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setMarkMoveLimit(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkMoveLimit] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkHaltPeriod (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setMarkHaltPeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkHaltPeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkStaleAfter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setMarkStaleAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkStaleAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfTimelock (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerSetBuilder&
    setTimelock(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTimelock] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultManagerSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultManagerSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultManagerSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
