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
class AMMClawbackBuilder;

/**
 * Transaction: AMMClawback
 * Type: ttAMM_CLAWBACK (31)
 * Delegable: Delegation::delegable
 * Amendment: featureAMMClawback
 * Privileges: mayDeleteAcct | overrideFreeze
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMClawbackBuilder to construct new transactions.
 */
class AMMClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_CLAWBACK;

    /**
     * Construct a AMMClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMClawback(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMClawback");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfHolder (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_.at(sfHolder);
    }

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
};

/**
 * Builder for AMMClawback transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMClawbackBuilder : public TransactionBuilderBase<AMMClawbackBuilder>
{
public:
    AMMClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMClawbackBuilder>(ttAMM_CLAWBACK, account, sequence, fee)
    {
        setHolder(holder);
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMClawbackBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for AMMClawbackBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfHolder (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Build and return the completed AMMClawback wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, AMMClawback>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, AMMClawback>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
