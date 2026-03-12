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

class DelegateSetBuilder;

/**
 * @brief Transaction: DelegateSet
 *
 * Type: ttDELEGATE_SET (64)
 * Delegable: Delegation::notDelegable
 * Amendment: featurePermissionDelegationV1_1
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DelegateSetBuilder to construct new transactions.
 */
class DelegateSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDELEGATE_SET;

    /**
     * @brief Construct a DelegateSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DelegateSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DelegateSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAuthorize (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAuthorize() const
    {
        return this->tx_->at(sfAuthorize);
    }
    /**
     * @brief Get sfPermissions (soeREQUIRED)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getPermissions() const
    {
        return this->tx_->getFieldArray(sfPermissions);
    }
};

/**
 * @brief Builder for DelegateSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DelegateSetBuilder : public TransactionBuilderBase<DelegateSetBuilder>
{
public:
    /**
     * @brief Construct a new DelegateSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param authorize The sfAuthorize field value.
     * @param permissions The sfPermissions field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    DelegateSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& authorize,                     STArray const& permissions,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DelegateSetBuilder>(ttDELEGATE_SET, account, sequence, fee)
    {
        setAuthorize(authorize);
        setPermissions(permissions);
    }

    /**
     * @brief Construct a DelegateSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    DelegateSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttDELEGATE_SET)
        {
            throw std::runtime_error("Invalid transaction type for DelegateSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAuthorize (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateSetBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfPermissions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateSetBuilder&
    setPermissions(STArray const& value)
    {
        object_.setFieldArray(sfPermissions, value);
        return *this;
    }

    /**
     * @brief Build and return the DelegateSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    DelegateSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return DelegateSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
