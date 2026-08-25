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

class ConfidentialMPTReencryptAuditorBuilder;

/**
 * @brief Transaction: ConfidentialMPTReencryptAuditor
 *
 * Type: ttCONFIDENTIAL_MPT_REENCRYPT_AUDITOR (90)
 * Delegable: Delegation::Delegable
 * Amendment: featureConfidentialTransfer
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTReencryptAuditorBuilder to construct new transactions.
 */
class ConfidentialMPTReencryptAuditor : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_REENCRYPT_AUDITOR;

    /**
     * @brief Construct a ConfidentialMPTReencryptAuditor transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTReencryptAuditor(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTReencryptAuditor");
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
     * @brief Get sfHolder (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_->at(sfHolder);
    }

    /**
     * @brief Get sfAuditorEncryptedBalance (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getAuditorEncryptedBalance() const
    {
        return this->tx_->at(sfAuditorEncryptedBalance);
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
 * @brief Builder for ConfidentialMPTReencryptAuditor transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTReencryptAuditorBuilder : public TransactionBuilderBase<ConfidentialMPTReencryptAuditorBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTReencryptAuditorBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param holder The sfHolder field value.
     * @param auditorEncryptedBalance The sfAuditorEncryptedBalance field value.
     * @param zKProof The sfZKProof field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTReencryptAuditorBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder,                     std::decay_t<typename SF_VL::type::value_type> const& auditorEncryptedBalance,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTReencryptAuditorBuilder>(ttCONFIDENTIAL_MPT_REENCRYPT_AUDITOR, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setHolder(holder);
        setAuditorEncryptedBalance(auditorEncryptedBalance);
        setZKProof(zKProof);
    }

    /**
     * @brief Construct a ConfidentialMPTReencryptAuditorBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTReencryptAuditorBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_REENCRYPT_AUDITOR)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTReencryptAuditorBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTReencryptAuditorBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTReencryptAuditorBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedBalance (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTReencryptAuditorBuilder&
    setAuditorEncryptedBalance(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptedBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTReencryptAuditorBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTReencryptAuditor wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTReencryptAuditor
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTReencryptAuditor{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
