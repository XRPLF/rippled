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

class AMMDepositBuilder;

/**
 * @brief Transaction: AMMDeposit
 *
 * Type: ttAMM_DEPOSIT (36)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMDepositBuilder to construct new transactions.
 */
class AMMDeposit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_DEPOSIT;

    /**
     * @brief Construct a AMMDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMDeposit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMDeposit");
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
     * @brief Get sfAmount (soeOPTIONAL)
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
     * @brief Get sfLPTokenOut (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLPTokenOut() const
    {
        if (hasLPTokenOut())
        {
            return this->tx_->at(sfLPTokenOut);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLPTokenOut is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLPTokenOut() const
    {
        return this->tx_->isFieldPresent(sfLPTokenOut);
    }

    /**
     * @brief Get sfTradingFee (soeOPTIONAL)
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
};

/**
 * @brief Builder for AMMDeposit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMDepositBuilder : public TransactionBuilderBase<AMMDepositBuilder>
{
public:
    /**
     * @brief Construct a new AMMDepositBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMDepositBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMDepositBuilder>(ttAMM_DEPOSIT, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    /**
     * @brief Construct a AMMDepositBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMDepositBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for AMMDepositBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount2 (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * @brief Set sfEPrice (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setEPrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfEPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfLPTokenOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setLPTokenOut(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenOut] = value;
        return *this;
    }

    /**
     * @brief Set sfTradingFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMDeposit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMDeposit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMDeposit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
