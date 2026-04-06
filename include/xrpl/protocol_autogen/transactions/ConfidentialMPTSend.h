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

class ConfidentialMPTSendBuilder;

/**
 * @brief Transaction: ConfidentialMPTSend
 *
 * Type: ttCONFIDENTIAL_MPT_SEND (88)
 * Delegable: Delegation::delegable
 * Amendment: featureConfidentialTransfer
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTSendBuilder to construct new transactions.
 */
class ConfidentialMPTSend : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_SEND;

    /**
     * @brief Construct a ConfidentialMPTSend transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTSend(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTSend");
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
     * @brief Get sfDestination (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_->at(sfDestination);
    }

    /**
     * @brief Get sfSenderEncryptedAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getSenderEncryptedAmount() const
    {
        return this->tx_->at(sfSenderEncryptedAmount);
    }

    /**
     * @brief Get sfDestinationEncryptedAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getDestinationEncryptedAmount() const
    {
        return this->tx_->at(sfDestinationEncryptedAmount);
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
     * @brief Get sfAmountCommitment (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getAmountCommitment() const
    {
        return this->tx_->at(sfAmountCommitment);
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

    /**
     * @brief Get sfCredentialIDs (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_->at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCredentialIDs is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }
};

/**
 * @brief Builder for ConfidentialMPTSend transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTSendBuilder : public TransactionBuilderBase<ConfidentialMPTSendBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTSendBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param destination The sfDestination field value.
     * @param senderEncryptedAmount The sfSenderEncryptedAmount field value.
     * @param destinationEncryptedAmount The sfDestinationEncryptedAmount field value.
     * @param issuerEncryptedAmount The sfIssuerEncryptedAmount field value.
     * @param zKProof The sfZKProof field value.
     * @param amountCommitment The sfAmountCommitment field value.
     * @param balanceCommitment The sfBalanceCommitment field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTSendBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_VL::type::value_type> const& senderEncryptedAmount,                     std::decay_t<typename SF_VL::type::value_type> const& destinationEncryptedAmount,                     std::decay_t<typename SF_VL::type::value_type> const& issuerEncryptedAmount,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                     std::decay_t<typename SF_VL::type::value_type> const& amountCommitment,                     std::decay_t<typename SF_VL::type::value_type> const& balanceCommitment,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTSendBuilder>(ttCONFIDENTIAL_MPT_SEND, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setDestination(destination);
        setSenderEncryptedAmount(senderEncryptedAmount);
        setDestinationEncryptedAmount(destinationEncryptedAmount);
        setIssuerEncryptedAmount(issuerEncryptedAmount);
        setZKProof(zKProof);
        setAmountCommitment(amountCommitment);
        setBalanceCommitment(balanceCommitment);
    }

    /**
     * @brief Construct a ConfidentialMPTSendBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTSendBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_SEND)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTSendBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfSenderEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setSenderEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSenderEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setDestinationEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDestinationEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerEncryptedAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setIssuerEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfIssuerEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setAuditorEncryptedAmount(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Set sfAmountCommitment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setAmountCommitment(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAmountCommitment] = value;
        return *this;
    }

    /**
     * @brief Set sfBalanceCommitment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setBalanceCommitment(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfBalanceCommitment] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTSendBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTSend wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTSend
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTSend{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
