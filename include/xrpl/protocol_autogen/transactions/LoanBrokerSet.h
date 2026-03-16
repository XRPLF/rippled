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

class LoanBrokerSetBuilder;

/**
 * @brief Transaction: LoanBrokerSet
 *
 * Type: ttLOAN_BROKER_SET (74)
 * Delegable: Delegation::notDelegable
 * Amendment: featureLendingProtocol
 * Privileges: createPseudoAcct | mayAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerSetBuilder to construct new transactions.
 */
class LoanBrokerSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_SET;

    /**
     * @brief Construct a LoanBrokerSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerSet");
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
     * @brief Get sfLoanBrokerID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getLoanBrokerID() const
    {
        if (hasLoanBrokerID())
        {
            return this->tx_->at(sfLoanBrokerID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLoanBrokerID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLoanBrokerID() const
    {
        return this->tx_->isFieldPresent(sfLoanBrokerID);
    }

    /**
     * @brief Get sfData (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_->at(sfData);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_->isFieldPresent(sfData);
    }

    /**
     * @brief Get sfManagementFeeRate (soeOPTIONAL)
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
     * @brief Get sfDebtMaximum (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getDebtMaximum() const
    {
        if (hasDebtMaximum())
        {
            return this->tx_->at(sfDebtMaximum);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDebtMaximum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDebtMaximum() const
    {
        return this->tx_->isFieldPresent(sfDebtMaximum);
    }

    /**
     * @brief Get sfCoverRateMinimum (soeOPTIONAL)
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
     * @brief Get sfCoverRateLiquidation (soeOPTIONAL)
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
};

/**
 * @brief Builder for LoanBrokerSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerSetBuilder : public TransactionBuilderBase<LoanBrokerSetBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param vaultID The sfVaultID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanBrokerSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerSetBuilder>(ttLOAN_BROKER_SET, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    /**
     * @brief Construct a LoanBrokerSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanBrokerSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_BROKER_SET)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanBrokerID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfManagementFeeRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setManagementFeeRate(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfManagementFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfDebtMaximum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setDebtMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDebtMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateLiquidation (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setCoverRateLiquidation(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateLiquidation] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanBrokerSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanBrokerSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanBrokerSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
