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

class ConfidentialMPTHolderKeyUpdateBuilder;

/**
 * @brief Transaction: ConfidentialMPTHolderKeyUpdate
 *
 * Type: ttCONFIDENTIAL_MPT_HOLDER_KEY_UPDATE (92)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureConfidentialMPTKeyRotation
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTHolderKeyUpdateBuilder to construct new transactions.
 */
class ConfidentialMPTHolderKeyUpdate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_HOLDER_KEY_UPDATE;

    /**
     * @brief Construct a ConfidentialMPTHolderKeyUpdate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTHolderKeyUpdate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTHolderKeyUpdate");
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
     * @brief Get sfHolderEncryptionKey (SoeOptional)
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
     * @brief Get sfConfidentialBalanceSpending (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getConfidentialBalanceSpending() const
    {
        if (hasConfidentialBalanceSpending())
        {
            return this->tx_->at(sfConfidentialBalanceSpending);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfConfidentialBalanceSpending is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasConfidentialBalanceSpending() const
    {
        return this->tx_->isFieldPresent(sfConfidentialBalanceSpending);
    }

    /**
     * @brief Get sfConfidentialBalanceInbox (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getConfidentialBalanceInbox() const
    {
        if (hasConfidentialBalanceInbox())
        {
            return this->tx_->at(sfConfidentialBalanceInbox);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfConfidentialBalanceInbox is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasConfidentialBalanceInbox() const
    {
        return this->tx_->isFieldPresent(sfConfidentialBalanceInbox);
    }

    /**
     * @brief Get sfZKProof (SoeOptional)
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
 * @brief Builder for ConfidentialMPTHolderKeyUpdate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTHolderKeyUpdateBuilder : public TransactionBuilderBase<ConfidentialMPTHolderKeyUpdateBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTHolderKeyUpdateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTHolderKeyUpdateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTHolderKeyUpdateBuilder>(ttCONFIDENTIAL_MPT_HOLDER_KEY_UPDATE, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    /**
     * @brief Construct a ConfidentialMPTHolderKeyUpdateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTHolderKeyUpdateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_HOLDER_KEY_UPDATE)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTHolderKeyUpdateBuilder");
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
    ConfidentialMPTHolderKeyUpdateBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfHolderEncryptionKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTHolderKeyUpdateBuilder&
    setHolderEncryptionKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfHolderEncryptionKey] = value;
        return *this;
    }

    /**
     * @brief Set sfConfidentialBalanceSpending (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTHolderKeyUpdateBuilder&
    setConfidentialBalanceSpending(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfConfidentialBalanceSpending] = value;
        return *this;
    }

    /**
     * @brief Set sfConfidentialBalanceInbox (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTHolderKeyUpdateBuilder&
    setConfidentialBalanceInbox(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfConfidentialBalanceInbox] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTHolderKeyUpdateBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTHolderKeyUpdate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTHolderKeyUpdate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTHolderKeyUpdate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
