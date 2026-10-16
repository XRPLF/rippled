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

class AMMCreateBuilder;

/**
 * @brief Transaction: AMMCreate
 *
 * Type: ttAMM_CREATE (35)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMM
 * Privileges: Privilege::CreatePseudoAcct | Privilege::MayCreateMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMCreateBuilder to construct new transactions.
 */
class AMMCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_CREATE;

    /**
     * @brief Construct a AMMCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * @brief Get sfAmount2 (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount2() const
    {
        return this->tx_->at(sfAmount2);
    }

    /**
     * @brief Get sfTradingFee (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getTradingFee() const
    {
        return this->tx_->at(sfTradingFee);
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
     * @brief Get sfFeeTier (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getFeeTier() const
    {
        if (hasFeeTier())
        {
            return this->tx_->at(sfFeeTier);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFeeTier is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFeeTier() const
    {
        return this->tx_->isFieldPresent(sfFeeTier);
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

    /**
     * @brief Get sfBinStep (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getBinStep() const
    {
        if (hasBinStep())
        {
            return this->tx_->at(sfBinStep);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBinStep is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBinStep() const
    {
        return this->tx_->isFieldPresent(sfBinStep);
    }
};

/**
 * @brief Builder for AMMCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMCreateBuilder : public TransactionBuilderBase<AMMCreateBuilder>
{
public:
    /**
     * @brief Construct a new AMMCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param amount2 The sfAmount2 field value.
     * @param tradingFee The sfTradingFee field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount2,                     std::decay_t<typename SF_UINT16::type::value_type> const& tradingFee,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMCreateBuilder>(ttAMM_CREATE, account, sequence, fee)
    {
        setAmount(amount);
        setAmount2(amount2);
        setTradingFee(tradingFee);
    }

    /**
     * @brief Construct a AMMCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for AMMCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount2 (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * @brief Set sfTradingFee (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * @brief Set sfCurveType (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setCurveType(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfCurveType] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeTier (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setFeeTier(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfFeeTier] = value;
        return *this;
    }

    /**
     * @brief Set sfAmplification (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setAmplification(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfAmplification] = value;
        return *this;
    }

    /**
     * @brief Set sfBinStep (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setBinStep(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfBinStep] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
