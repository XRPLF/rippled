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

// Forward declaration
class AMMBidBuilder;

/**
 * Transaction: AMMBid
 * Type: ttAMM_BID (39)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMBidBuilder to construct new transactions.
 */
class AMMBid : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_BID;

    /**
     * Construct a AMMBid transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMBid(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMBid");
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
        return this->tx_->at(sfAsset);
    }

    /**
     * Get sfAsset2 (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->tx_->at(sfAsset2);
    }

    /**
     * Get sfBidMin (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBidMin() const
    {
        if (hasBidMin())
        {
            return this->tx_->at(sfBidMin);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBidMin() const
    {
        return this->tx_->isFieldPresent(sfBidMin);
    }

    /**
     * Get sfBidMax (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBidMax() const
    {
        if (hasBidMax())
        {
            return this->tx_->at(sfBidMax);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBidMax() const
    {
        return this->tx_->isFieldPresent(sfBidMax);
    }
    /**
     * Get sfAuthAccounts (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuthAccounts() const
    {
        if (this->tx_->isFieldPresent(sfAuthAccounts))
            return this->tx_->getFieldArray(sfAuthAccounts);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuthAccounts() const
    {
        return this->tx_->isFieldPresent(sfAuthAccounts);
    }
};

/**
 * Builder for AMMBid transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMBidBuilder : public TransactionBuilderBase<AMMBidBuilder>
{
public:
    AMMBidBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMBidBuilder>(ttAMM_BID, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMBidBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_BID)
        {
            throw std::runtime_error("Invalid transaction type for AMMBidBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBidBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBidBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfBidMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBidBuilder&
    setBidMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBidMin] = value;
        return *this;
    }

    /**
     * Set sfBidMax (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBidBuilder&
    setBidMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBidMax] = value;
        return *this;
    }

    /**
     * Set sfAuthAccounts (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBidBuilder&
    setAuthAccounts(STArray const& value)
    {
        object_.setFieldArray(sfAuthAccounts, value);
        return *this;
    }

    /**
     * Build and return the AMMBid wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    AMMBid
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMBid{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
