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

class TrustSetBuilder;

/**
 * @brief Transaction: TrustSet
 *
 * Type: ttTRUST_SET (20)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TrustSetBuilder to construct new transactions.
 */
class TrustSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTRUST_SET;

    /**
     * @brief Construct a TrustSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TrustSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TrustSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLimitAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLimitAmount() const
    {
        if (hasLimitAmount())
        {
            return this->tx_->at(sfLimitAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLimitAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLimitAmount() const
    {
        return this->tx_->isFieldPresent(sfLimitAmount);
    }

    /**
     * @brief Get sfQualityIn (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getQualityIn() const
    {
        if (hasQualityIn())
        {
            return this->tx_->at(sfQualityIn);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfQualityIn is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasQualityIn() const
    {
        return this->tx_->isFieldPresent(sfQualityIn);
    }

    /**
     * @brief Get sfQualityOut (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getQualityOut() const
    {
        if (hasQualityOut())
        {
            return this->tx_->at(sfQualityOut);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfQualityOut is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasQualityOut() const
    {
        return this->tx_->isFieldPresent(sfQualityOut);
    }
};

/**
 * @brief Builder for TrustSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TrustSetBuilder : public TransactionBuilderBase<TrustSetBuilder>
{
public:
    /**
     * @brief Construct a new TrustSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TrustSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TrustSetBuilder>(ttTRUST_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a TrustSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TrustSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTRUST_SET)
        {
            throw std::runtime_error("Invalid transaction type for TrustSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLimitAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    TrustSetBuilder&
    setLimitAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLimitAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfQualityIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    TrustSetBuilder&
    setQualityIn(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfQualityIn] = value;
        return *this;
    }

    /**
     * @brief Set sfQualityOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    TrustSetBuilder&
    setQualityOut(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfQualityOut] = value;
        return *this;
    }

    /**
     * @brief Build and return the TrustSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TrustSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TrustSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
