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
class AMMWithdrawBuilder;

/**
 * Transaction: AMMWithdraw
 * Type: ttAMM_WITHDRAW (37)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: mayDeleteAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMWithdrawBuilder to construct new transactions.
 */
class AMMWithdraw : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_WITHDRAW;

    /**
     * Construct a AMMWithdraw transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMWithdraw(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMWithdraw");
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
     * Get sfLPTokenIn (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getLPTokenIn() const
    {
        if (hasLPTokenIn())
        {
            return this->tx_.at(sfLPTokenIn);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLPTokenIn() const
    {
        return this->tx_.isFieldPresent(sfLPTokenIn);
    }
};

/**
 * Builder for AMMWithdraw transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMWithdrawBuilder : public TransactionBuilderBase<AMMWithdrawBuilder>
{
public:
    AMMWithdrawBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMWithdrawBuilder>(ttAMM_WITHDRAW, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMWithdrawBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_WITHDRAW)
        {
            throw std::runtime_error("Invalid transaction type for AMMWithdrawBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfAmount2 (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * Set sfEPrice (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setEPrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfEPrice] = value;
        return *this;
    }

    /**
     * Set sfLPTokenIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMWithdrawBuilder&
    setLPTokenIn(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenIn] = value;
        return *this;
    }

    /**
     * Build and return the completed AMMWithdraw wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, AMMWithdraw>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, AMMWithdraw>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
