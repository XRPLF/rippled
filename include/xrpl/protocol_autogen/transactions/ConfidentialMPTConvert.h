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

class ConfidentialMPTConvertBuilder;

/**
 * @brief Transaction: ConfidentialMPTConvert
 *
 * Type: ttCONFIDENTIAL_MPT_CONVERT (85)
 * Delegable: Delegation::delegable
 * Amendment: featureConfidentialTransfer
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTConvertBuilder to construct new transactions.
 */
class ConfidentialMPTConvert : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_CONVERT;

    /**
     * @brief Construct a ConfidentialMPTConvert transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTConvert(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTConvert");
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
     * @brief Get sfHolderEncryptionKey (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getHolderEncryptionKey() const
    {
        if (hasHolderEncryptionKey())
        {
            return this->tx_->at(sfHolderEncryptionKey);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfHolderEncryptionKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHolderEncryptionKey() const
    {
        return this->tx_->isFieldPresent(sfHolderEncryptionKey);
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
     * @brief Get sfZKProof (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getZKProof() const
    {
        if (hasZKProof())
        {
            return this->tx_->at(sfZKProof);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfZKProof is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasZKProof() const
    {
        return this->tx_->isFieldPresent(sfZKProof);
    }
};

/**
 * @brief Builder for ConfidentialMPTConvert transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTConvertBuilder : public TransactionBuilderBase<ConfidentialMPTConvertBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTConvertBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param mPTAmount The sfMPTAmount field value.
     * @param holderEncryptedAmount The sfHolderEncryptedAmount field value.
     * @param issuerEncryptedAmount The sfIssuerEncryptedAmount field value.
     * @param blindingFactor The sfBlindingFactor field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTConvertBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_UINT64::type::value_type> const& mPTAmount,                     std::decay_t<typename SF_VL::type::value_type> const& holderEncryptedAmount,                     std::decay_t<typename SF_VL::type::value_type> const& issuerEncryptedAmount,                     std::decay_t<typename SF_UINT256::type::value_type> const& blindingFactor,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTConvertBuilder>(ttCONFIDENTIAL_MPT_CONVERT, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setMPTAmount(mPTAmount);
        setHolderEncryptedAmount(holderEncryptedAmount);
        setIssuerEncryptedAmount(issuerEncryptedAmount);
        setBlindingFactor(blindingFactor);
    }

    /**
     * @brief Construct a ConfidentialMPTConvertBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTConvertBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_CONVERT)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTConvertBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setMPTAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMPTAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfHolderEncryptionKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setHolderEncryptionKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfHolderEncryptionKey] = value;
        return *this;
    }

    /**
     * @brief Set sfHolderEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setHolderEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfHolderEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setIssuerEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfIssuerEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setAuditorEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfBlindingFactor (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setBlindingFactor(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBlindingFactor] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTConvertBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTConvert wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTConvert
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTConvert{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
