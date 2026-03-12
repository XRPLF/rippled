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
class AMMDeleteBuilder;

/**
 * Transaction: AMMDelete
 * Type: ttAMM_DELETE (40)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: mustDeleteAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMDeleteBuilder to construct new transactions.
 */
class AMMDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_DELETE;

    /**
     * Construct a AMMDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMDelete");
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
};

/**
 * Builder for AMMDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMDeleteBuilder : public TransactionBuilderBase<AMMDeleteBuilder>
{
public:
    AMMDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMDeleteBuilder>(ttAMM_DELETE, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for AMMDeleteBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Build and return the AMMDelete wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    AMMDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
