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

class AMMWithdrawBuilder;

/**
 * @brief Transaction: AMMWithdraw
 *
 * Type: ttAMM_WITHDRAW (37)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: mayDeleteAcct | mayAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMWithdrawBuilder to construct new transactions.
 */
class AMMWithdraw : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_WITHDRAW;

    /**
     * @brief Construct a AMMWithdraw transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMWithdraw(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMWithdraw");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (soeREQUIRED)
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
     * @brief Get sfAsset2 (soeREQUIRED)
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
     * @brief Get sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }

    /**
     * @brief Get sfAmount2 (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount2() const
    {
        if (hasAmount2())
        {
            return this->tx_->at(sfAmount2);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount2 is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount2() const
    {
        return this->tx_->isFieldPresent(sfAmount2);
    }

    /**
     * @brief Get sfEPrice (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getEPrice() const
    {
        if (hasEPrice())
        {
            return this->tx_->at(sfEPrice);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfEPrice is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEPrice() const
    {
        return this->tx_->isFieldPresent(sfEPrice);
    }

    /**
     * @brief Get sfLPTokenIn (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLPTokenIn() const
    {
        if (hasLPTokenIn())
        {
            return this->tx_->at(sfLPTokenIn);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLPTokenIn is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLPTokenIn() const
    {
        return this->tx_->isFieldPresent(sfLPTokenIn);
    }
};

/**
 * @brief Builder for AMMWithdraw transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMWithdrawBuilder : public TransactionBuilderBase<AMMWithdrawBuilder>
{
public:
    /**
     * @brief Construct a new AMMWithdrawBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMWithdrawBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMWithdrawBuilder>(ttAMM_WITHDRAW, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    /**
     * @brief Construct a AMMWithdrawBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMWithdrawBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_WITHDRAW)
        {
            throw std::runtime_error("Invalid transaction type for AMMWithdrawBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAsset (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount2 (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * @brief Set sfEPrice (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setEPrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfEPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfLPTokenIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setLPTokenIn(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenIn] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMWithdraw wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMWithdraw
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMWithdraw{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
