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

class ConfidentialMPTConvertBackBuilder;

/**
 * @brief Transaction: ConfidentialMPTConvertBack
 *
 * Type: ttCONFIDENTIAL_MPT_CONVERT_BACK (87)
 * Delegable: Delegation::delegable
 * Amendment: featureConfidentialTransfer
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTConvertBackBuilder to construct new transactions.
 */
class ConfidentialMPTConvertBack : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_CONVERT_BACK;

    /**
     * @brief Construct a ConfidentialMPTConvertBack transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTConvertBack(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTConvertBack");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMPTokenIssuanceID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_->at(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfMPTAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getMPTAmount() const
    {
        return this->tx_->at(sfMPTAmount);
    }

    /**
     * @brief Get sfHolderEncryptedAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getHolderEncryptedAmount() const
    {
        return this->tx_->at(sfHolderEncryptedAmount);
    }

    /**
     * @brief Get sfIssuerEncryptedAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getIssuerEncryptedAmount() const
    {
        return this->tx_->at(sfIssuerEncryptedAmount);
    }

    /**
     * @brief Get sfAuditorEncryptedAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getAuditorEncryptedAmount() const
    {
        if (hasAuditorEncryptedAmount())
        {
            return this->tx_->at(sfAuditorEncryptedAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuditorEncryptedAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuditorEncryptedAmount() const
    {
        return this->tx_->isFieldPresent(sfAuditorEncryptedAmount);
    }

    /**
     * @brief Get sfBlindingFactor (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBlindingFactor() const
    {
        return this->tx_->at(sfBlindingFactor);
    }

    /**
     * @brief Get sfZKProof (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getZKProof() const
    {
        return this->tx_->at(sfZKProof);
    }

    /**
     * @brief Get sfBalanceCommitment (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getBalanceCommitment() const
    {
        return this->tx_->at(sfBalanceCommitment);
    }
};

/**
 * @brief Builder for ConfidentialMPTConvertBack transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTConvertBackBuilder : public TransactionBuilderBase<ConfidentialMPTConvertBackBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTConvertBackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param mPTAmount The sfMPTAmount field value.
     * @param holderEncryptedAmount The sfHolderEncryptedAmount field value.
     * @param issuerEncryptedAmount The sfIssuerEncryptedAmount field value.
     * @param blindingFactor The sfBlindingFactor field value.
     * @param zKProof The sfZKProof field value.
     * @param balanceCommitment The sfBalanceCommitment field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTConvertBackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_UINT64::type::value_type> const& mPTAmount,                     std::decay_t<typename SF_VL::type::value_type> const& holderEncryptedAmount,                     std::decay_t<typename SF_VL::type::value_type> const& issuerEncryptedAmount,                     std::decay_t<typename SF_UINT256::type::value_type> const& blindingFactor,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                     std::decay_t<typename SF_VL::type::value_type> const& balanceCommitment,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTConvertBackBuilder>(ttCONFIDENTIAL_MPT_CONVERT_BACK, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setMPTAmount(mPTAmount);
        setHolderEncryptedAmount(holderEncryptedAmount);
        setIssuerEncryptedAmount(issuerEncryptedAmount);
        setBlindingFactor(blindingFactor);
        setZKProof(zKProof);
        setBalanceCommitment(balanceCommitment);
    }

    /**
     * @brief Construct a ConfidentialMPTConvertBackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTConvertBackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_CONVERT_BACK)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTConvertBackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setMPTAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMPTAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfHolderEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setHolderEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfHolderEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setIssuerEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfIssuerEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setAuditorEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfBlindingFactor (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setBlindingFactor(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBlindingFactor] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Set sfBalanceCommitment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBackBuilder&
    setBalanceCommitment(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfBalanceCommitment] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTConvertBack wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTConvertBack
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTConvertBack{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
