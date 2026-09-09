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

class ConfidentialMPTMirrorUpdateBuilder;

/**
 * @brief Transaction: ConfidentialMPTMirrorUpdate
 *
 * Type: ttCONFIDENTIAL_MPT_MIRROR_UPDATE (92)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureConfidentialMPTKeyRotation
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTMirrorUpdateBuilder to construct new transactions.
 */
class ConfidentialMPTMirrorUpdate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_MIRROR_UPDATE;

    /**
     * @brief Construct a ConfidentialMPTMirrorUpdate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTMirrorUpdate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTMirrorUpdate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMPTokenIssuanceID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_->at(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfHolder (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getHolder() const
    {
        if (hasHolder())
        {
            return this->tx_->at(sfHolder);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfHolder is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHolder() const
    {
        return this->tx_->isFieldPresent(sfHolder);
    }

    /**
     * @brief Get sfIssuerEncryptedAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getIssuerEncryptedAmount() const
    {
        if (hasIssuerEncryptedAmount())
        {
            return this->tx_->at(sfIssuerEncryptedAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuerEncryptedAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuerEncryptedAmount() const
    {
        return this->tx_->isFieldPresent(sfIssuerEncryptedAmount);
    }

    /**
     * @brief Get sfAuditorEncryptedAmount (SoeOptional)
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
     * @brief Get sfZKProof (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getZKProof() const
    {
        return this->tx_->at(sfZKProof);
    }
};

/**
 * @brief Builder for ConfidentialMPTMirrorUpdate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTMirrorUpdateBuilder : public TransactionBuilderBase<ConfidentialMPTMirrorUpdateBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTMirrorUpdateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param zKProof The sfZKProof field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTMirrorUpdateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTMirrorUpdateBuilder>(ttCONFIDENTIAL_MPT_MIRROR_UPDATE, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setZKProof(zKProof);
    }

    /**
     * @brief Construct a ConfidentialMPTMirrorUpdateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTMirrorUpdateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_MIRROR_UPDATE)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTMirrorUpdateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMPTokenIssuanceID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTMirrorUpdateBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTMirrorUpdateBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerEncryptedAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTMirrorUpdateBuilder&
    setIssuerEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfIssuerEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTMirrorUpdateBuilder&
    setAuditorEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTMirrorUpdateBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTMirrorUpdate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTMirrorUpdate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTMirrorUpdate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
