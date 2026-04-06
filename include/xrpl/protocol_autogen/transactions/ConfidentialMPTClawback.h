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

class ConfidentialMPTClawbackBuilder;

/**
 * @brief Transaction: ConfidentialMPTClawback
 *
 * Type: ttCONFIDENTIAL_MPT_CLAWBACK (89)
 * Delegable: Delegation::delegable
 * Amendment: featureConfidentialTransfer
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ConfidentialMPTClawbackBuilder to construct new transactions.
 */
class ConfidentialMPTClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONFIDENTIAL_MPT_CLAWBACK;

    /**
     * @brief Construct a ConfidentialMPTClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ConfidentialMPTClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTClawback");
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
     * @brief Get sfHolder (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_->at(sfHolder);
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
     * @brief Get sfZKProof (soeREQUIRED)
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
 * @brief Builder for ConfidentialMPTClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ConfidentialMPTClawbackBuilder : public TransactionBuilderBase<ConfidentialMPTClawbackBuilder>
{
public:
    /**
     * @brief Construct a new ConfidentialMPTClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param holder The sfHolder field value.
     * @param mPTAmount The sfMPTAmount field value.
     * @param zKProof The sfZKProof field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ConfidentialMPTClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder,                     std::decay_t<typename SF_UINT64::type::value_type> const& mPTAmount,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ConfidentialMPTClawbackBuilder>(ttCONFIDENTIAL_MPT_CLAWBACK, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setHolder(holder);
        setMPTAmount(mPTAmount);
        setZKProof(zKProof);
    }

    /**
     * @brief Construct a ConfidentialMPTClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ConfidentialMPTClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONFIDENTIAL_MPT_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for ConfidentialMPTClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTClawbackBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTClawbackBuilder&
    setMPTAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMPTAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ConfidentialMPTClawbackBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the ConfidentialMPTClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ConfidentialMPTClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ConfidentialMPTClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
