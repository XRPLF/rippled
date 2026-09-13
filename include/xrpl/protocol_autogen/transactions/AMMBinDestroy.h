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

class AMMBinDestroyBuilder;

/**
 * @brief Transaction: AMMBinDestroy
 *
 * Type: ttAMM_BIN_DESTROY (111)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMMCurves
 * Privileges: Privilege::DestroyMptIssuance
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMBinDestroyBuilder to construct new transactions.
 */
class AMMBinDestroy : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_BIN_DESTROY;

    /**
     * @brief Construct a AMMBinDestroy transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMBinDestroy(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMBinDestroy");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (SoeRequired)
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
     * @brief Get sfAsset2 (SoeRequired)
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
     * @brief Get sfBinID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getBinID() const
    {
        return this->tx_->at(sfBinID);
    }
};

/**
 * @brief Builder for AMMBinDestroy transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMBinDestroyBuilder : public TransactionBuilderBase<AMMBinDestroyBuilder>
{
public:
    /**
     * @brief Construct a new AMMBinDestroyBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param binID The sfBinID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMBinDestroyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                     std::decay_t<typename SF_INT32::type::value_type> const& binID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMBinDestroyBuilder>(ttAMM_BIN_DESTROY, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
        setBinID(binID);
    }

    /**
     * @brief Construct a AMMBinDestroyBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMBinDestroyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_BIN_DESTROY)
        {
            throw std::runtime_error("Invalid transaction type for AMMBinDestroyBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMBinDestroyBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    AMMBinDestroyBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfBinID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinDestroyBuilder&
    setBinID(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfBinID] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMBinDestroy wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMBinDestroy
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMBinDestroy{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
