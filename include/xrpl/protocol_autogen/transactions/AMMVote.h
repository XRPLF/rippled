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

class AMMVoteBuilder;

/**
 * @brief Transaction: AMMVote
 *
 * Type: ttAMM_VOTE (38)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMM
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMVoteBuilder to construct new transactions.
 */
class AMMVote : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_VOTE;

    /**
     * @brief Construct a AMMVote transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMVote(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMVote");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_->at(sfAsset);
    }

    /**
     * @brief Get sfAsset2 (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->tx_->at(sfAsset2);
    }

    /**
     * @brief Get sfTradingFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTradingFee() const
    {
        if (hasTradingFee())
        {
            return this->tx_->at(sfTradingFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTradingFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTradingFee() const
    {
        return this->tx_->isFieldPresent(sfTradingFee);
    }

    /**
     * @brief Get sfCurveType (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getCurveType() const
    {
        if (hasCurveType())
        {
            return this->tx_->at(sfCurveType);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCurveType is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCurveType() const
    {
        return this->tx_->isFieldPresent(sfCurveType);
    }

    /**
     * @brief Get sfAmplification (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getAmplification() const
    {
        if (hasAmplification())
        {
            return this->tx_->at(sfAmplification);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmplification is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmplification() const
    {
        return this->tx_->isFieldPresent(sfAmplification);
    }
};

/**
 * @brief Builder for AMMVote transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMVoteBuilder : public TransactionBuilderBase<AMMVoteBuilder>
{
public:
    /**
     * @brief Construct a new AMMVoteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMVoteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMVoteBuilder>(ttAMM_VOTE, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    /**
     * @brief Construct a AMMVoteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMVoteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_VOTE)
        {
            throw std::runtime_error("Invalid transaction type for AMMVoteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfTradingFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * @brief Set sfCurveType (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setCurveType(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfCurveType] = value;
        return *this;
    }

    /**
     * @brief Set sfAmplification (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAmplification(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfAmplification] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMVote wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMVote
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMVote{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
