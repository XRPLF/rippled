#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class AMMDepositBuilder;

/**
 * Transaction: AMMDeposit
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
     * Construct a AMMDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMDeposit(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMDeposit");
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
     * Get sfAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_.at(sfAmount);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_.isFieldPresent(sfAmount);
    }

    /**
     * Get sfAmount2 (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount2() const
    {
        if (hasAmount2())
        {
            return this->tx_.at(sfAmount2);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmount2() const
    {
        return this->tx_.isFieldPresent(sfAmount2);
    }

    /**
     * Get sfEPrice (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getEPrice() const
    {
        if (hasEPrice())
        {
            return this->tx_.at(sfEPrice);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasEPrice() const
    {
        return this->tx_.isFieldPresent(sfEPrice);
    }

    /**
     * Get sfLPTokenOut (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLPTokenOut() const
    {
        if (hasLPTokenOut())
        {
            return this->tx_.at(sfLPTokenOut);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLPTokenOut() const
    {
        return this->tx_.isFieldPresent(sfLPTokenOut);
    }

    /**
     * Get sfTradingFee (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTradingFee() const
    {
        if (hasTradingFee())
        {
            return this->tx_.at(sfTradingFee);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTradingFee() const
    {
        return this->tx_.isFieldPresent(sfTradingFee);
    }
};

/**
 * Builder for AMMDeposit transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMDepositBuilder : public TransactionBuilderBase<AMMDepositBuilder>
{
public:
    AMMDepositBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2)
        : TransactionBuilderBase<AMMDepositBuilder>(account, sequence, fee, signingPubKey, ttAMM_DEPOSIT)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMDepositBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for AMMDepositBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfAmount2 (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * Set sfEPrice (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setEPrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfEPrice] = value;
        return *this;
    }

    /**
     * Set sfLPTokenOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setLPTokenOut(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenOut] = value;
        return *this;
    }

    /**
     * Set sfTradingFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMDepositBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * Build and return the completed AMMDeposit wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    AMMDeposit
    build()
    {
        return AMMDeposit(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions