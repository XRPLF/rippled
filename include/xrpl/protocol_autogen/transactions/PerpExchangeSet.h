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

class PerpExchangeSetBuilder;

/**
 * @brief Transaction: PerpExchangeSet
 *
 * Type: ttPERP_EXCHANGE_SET (103)
 * Delegable: Delegation::NotDelegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::CreatePseudoAcct | Privilege::MayAuthorizeMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpExchangeSetBuilder to construct new transactions.
 */
class PerpExchangeSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_EXCHANGE_SET;

    /**
     * @brief Construct a PerpExchangeSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpExchangeSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpExchangeSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfExchangeID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getExchangeID() const
    {
        if (hasExchangeID())
        {
            return this->tx_->at(sfExchangeID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfExchangeID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExchangeID() const
    {
        return this->tx_->isFieldPresent(sfExchangeID);
    }

    /**
     * @brief Get sfAsset (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ISSUE::type::value_type>
    getAsset() const
    {
        if (hasAsset())
        {
            return this->tx_->at(sfAsset);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAsset is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAsset() const
    {
        return this->tx_->isFieldPresent(sfAsset);
    }

    /**
     * @brief Get sfScale (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
        {
            return this->tx_->at(sfScale);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->tx_->isFieldPresent(sfScale);
    }

    /**
     * @brief Get sfDomainID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_->at(sfDomainID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }

    /**
     * @brief Get sfMakerFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMakerFee() const
    {
        if (hasMakerFee())
        {
            return this->tx_->at(sfMakerFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMakerFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMakerFee() const
    {
        return this->tx_->isFieldPresent(sfMakerFee);
    }

    /**
     * @brief Get sfTakerFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTakerFee() const
    {
        if (hasTakerFee())
        {
            return this->tx_->at(sfTakerFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerFee() const
    {
        return this->tx_->isFieldPresent(sfTakerFee);
    }

    /**
     * @brief Get sfOwnerFeeShare (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOwnerFeeShare() const
    {
        if (hasOwnerFeeShare())
        {
            return this->tx_->at(sfOwnerFeeShare);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerFeeShare is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerFeeShare() const
    {
        return this->tx_->isFieldPresent(sfOwnerFeeShare);
    }

    /**
     * @brief Get sfBuilderFeeMax (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBuilderFeeMax() const
    {
        if (hasBuilderFeeMax())
        {
            return this->tx_->at(sfBuilderFeeMax);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilderFeeMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilderFeeMax() const
    {
        return this->tx_->isFieldPresent(sfBuilderFeeMax);
    }

    /**
     * @brief Get sfCoverRateMinimum (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateMinimum() const
    {
        if (hasCoverRateMinimum())
        {
            return this->tx_->at(sfCoverRateMinimum);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateMinimum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateMinimum() const
    {
        return this->tx_->isFieldPresent(sfCoverRateMinimum);
    }

    /**
     * @brief Get sfLiquidationPenalty (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLiquidationPenalty() const
    {
        if (hasLiquidationPenalty())
        {
            return this->tx_->at(sfLiquidationPenalty);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationPenalty is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationPenalty() const
    {
        return this->tx_->isFieldPresent(sfLiquidationPenalty);
    }

    /**
     * @brief Get sfLiquidationRewardMin (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLiquidationRewardMin() const
    {
        if (hasLiquidationRewardMin())
        {
            return this->tx_->at(sfLiquidationRewardMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationRewardMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationRewardMin() const
    {
        return this->tx_->isFieldPresent(sfLiquidationRewardMin);
    }

    /**
     * @brief Get sfLiquidationChunkMin (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLiquidationChunkMin() const
    {
        if (hasLiquidationChunkMin())
        {
            return this->tx_->at(sfLiquidationChunkMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationChunkMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationChunkMin() const
    {
        return this->tx_->isFieldPresent(sfLiquidationChunkMin);
    }

    /**
     * @brief Get sfData (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_->at(sfData);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_->isFieldPresent(sfData);
    }
};

/**
 * @brief Builder for PerpExchangeSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpExchangeSetBuilder : public TransactionBuilderBase<PerpExchangeSetBuilder>
{
public:
    /**
     * @brief Construct a new PerpExchangeSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpExchangeSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpExchangeSetBuilder>(ttPERP_EXCHANGE_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a PerpExchangeSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpExchangeSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_EXCHANGE_SET)
        {
            throw std::runtime_error("Invalid transaction type for PerpExchangeSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfExchangeID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfScale (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfMakerFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setMakerFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMakerFee] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setTakerFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTakerFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerFeeShare (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setOwnerFeeShare(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOwnerFeeShare] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilderFeeMax (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setBuilderFeeMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBuilderFeeMax] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationPenalty (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setLiquidationPenalty(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationPenalty] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardMin (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setLiquidationRewardMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationRewardMin] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationChunkMin (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setLiquidationChunkMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationChunkMin] = value;
        return *this;
    }

    /**
     * @brief Set sfData (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpExchangeSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpExchangeSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpExchangeSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
