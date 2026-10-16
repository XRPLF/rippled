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

class AMMCollectFeesBuilder;

/**
 * @brief Transaction: AMMCollectFees
 *
 * Type: ttAMM_COLLECT_FEES (112)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMMCurves
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMCollectFeesBuilder to construct new transactions.
 */
class AMMCollectFees : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_COLLECT_FEES;

    /**
     * @brief Construct a AMMCollectFees transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMCollectFees(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMCollectFees");
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
     * @brief Get sfPositionID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPositionID() const
    {
        if (hasPositionID())
        {
            return this->tx_->at(sfPositionID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPositionID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPositionID() const
    {
        return this->tx_->isFieldPresent(sfPositionID);
    }

    /**
     * @brief Get sfBinID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_INT32::type::value_type>
    getBinID() const
    {
        if (hasBinID())
        {
            return this->tx_->at(sfBinID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBinID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBinID() const
    {
        return this->tx_->isFieldPresent(sfBinID);
    }
};

/**
 * @brief Builder for AMMCollectFees transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMCollectFeesBuilder : public TransactionBuilderBase<AMMCollectFeesBuilder>
{
public:
    /**
     * @brief Construct a new AMMCollectFeesBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMCollectFeesBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMCollectFeesBuilder>(ttAMM_COLLECT_FEES, account, sequence, fee)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    /**
     * @brief Construct a AMMCollectFeesBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMCollectFeesBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_COLLECT_FEES)
        {
            throw std::runtime_error("Invalid transaction type for AMMCollectFeesBuilder");
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
    AMMCollectFeesBuilder&
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
    AMMCollectFeesBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfCurveType (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCollectFeesBuilder&
    setCurveType(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfCurveType] = value;
        return *this;
    }

    /**
     * @brief Set sfPositionID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCollectFeesBuilder&
    setPositionID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPositionID] = value;
        return *this;
    }

    /**
     * @brief Set sfBinID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMCollectFeesBuilder&
    setBinID(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfBinID] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMCollectFees wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMCollectFees
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMCollectFees{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
