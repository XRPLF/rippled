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
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: noPriv
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
     * @brief Get sfAsset (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_->at(sfAsset);
    }

    /**
     * @brief Get sfAsset2 (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->tx_->at(sfAsset2);
    }

    /**
     * @brief Get sfTradingFee (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getTradingFee() const
    {
        return this->tx_->at(sfTradingFee);
    }
};

/**
 * @brief Builder for AMMVote transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
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
     * @param tradingFee The sfTradingFee field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMVoteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                     std::decay_t<typename SF_UINT16::type::value_type> const& tradingFee,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMVoteBuilder>(ttAMM_VOTE, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
        setTradingFee(tradingFee);
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

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfTradingFee (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
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
