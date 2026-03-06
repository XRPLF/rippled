// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class AMMVoteBuilder;

/**
 * Transaction: AMMVote
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
     * Construct a AMMVote transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMVote(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMVote");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAsset (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_.at(sfAsset);
    }

    /**
     * Get sfAsset2 (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->tx_.at(sfAsset2);
    }

    /**
     * Get sfTradingFee (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getTradingFee() const
    {
        return this->tx_.at(sfTradingFee);
    }
};

/**
 * Builder for AMMVote transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMVoteBuilder : public TransactionBuilderBase<AMMVoteBuilder>
{
public:
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

    AMMVoteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_VOTE)
        {
            throw std::runtime_error("Invalid transaction type for AMMVoteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfTradingFee (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMVoteBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * Build and return the completed AMMVote wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, AMMVote>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, AMMVote>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
