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

class AMMClawbackBuilder;

/**
 * @brief Transaction: AMMClawback
 *
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
     * @brief Construct a AMMClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMClawback");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfHolder (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_->at(sfHolder);
    }

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
};

/**
 * @brief Builder for AMMClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMClawbackBuilder : public TransactionBuilderBase<AMMClawbackBuilder>
{
public:
    /**
     * @brief Construct a new AMMClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param holder The sfHolder field value.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
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

    /**
     * @brief Construct a AMMClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for AMMClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfHolder (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
