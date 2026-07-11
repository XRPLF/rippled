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

class SponsorshipSetBuilder;

/**
 * @brief Transaction: SponsorshipSet
 *
 * Type: ttSPONSORSHIP_SET (91)
 * Delegable: Delegation::Delegable
 * Amendment: featureSponsor
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SponsorshipSetBuilder to construct new transactions.
 */
class SponsorshipSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSPONSORSHIP_SET;

    /**
     * @brief Construct a SponsorshipSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SponsorshipSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SponsorshipSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCounterpartySponsor (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getCounterpartySponsor() const
    {
        if (hasCounterpartySponsor())
        {
            return this->tx_->at(sfCounterpartySponsor);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterpartySponsor is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterpartySponsor() const
    {
        return this->tx_->isFieldPresent(sfCounterpartySponsor);
    }

    /**
     * @brief Get sfSponsee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getSponsee() const
    {
        if (hasSponsee())
        {
            return this->tx_->at(sfSponsee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSponsee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSponsee() const
    {
        return this->tx_->isFieldPresent(sfSponsee);
    }

    /**
     * @brief Get sfFeeAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getFeeAmount() const
    {
        if (hasFeeAmount())
        {
            return this->tx_->at(sfFeeAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFeeAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFeeAmount() const
    {
        return this->tx_->isFieldPresent(sfFeeAmount);
    }

    /**
     * @brief Get sfMaxFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMaxFee() const
    {
        if (hasMaxFee())
        {
            return this->tx_->at(sfMaxFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaxFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaxFee() const
    {
        return this->tx_->isFieldPresent(sfMaxFee);
    }

    /**
     * @brief Get sfRemainingOwnerCount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getRemainingOwnerCount() const
    {
        if (hasRemainingOwnerCount())
        {
            return this->tx_->at(sfRemainingOwnerCount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfRemainingOwnerCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasRemainingOwnerCount() const
    {
        return this->tx_->isFieldPresent(sfRemainingOwnerCount);
    }
};

/**
 * @brief Builder for SponsorshipSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SponsorshipSetBuilder : public TransactionBuilderBase<SponsorshipSetBuilder>
{
public:
    /**
     * @brief Construct a new SponsorshipSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SponsorshipSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SponsorshipSetBuilder>(ttSPONSORSHIP_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a SponsorshipSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SponsorshipSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSPONSORSHIP_SET)
        {
            throw std::runtime_error("Invalid transaction type for SponsorshipSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCounterpartySponsor (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipSetBuilder&
    setCounterpartySponsor(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterpartySponsor] = value;
        return *this;
    }

    /**
     * @brief Set sfSponsee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipSetBuilder&
    setSponsee(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSponsee] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipSetBuilder&
    setFeeAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfFeeAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMaxFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipSetBuilder&
    setMaxFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMaxFee] = value;
        return *this;
    }

    /**
     * @brief Set sfRemainingOwnerCount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipSetBuilder&
    setRemainingOwnerCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfRemainingOwnerCount] = value;
        return *this;
    }

    /**
     * @brief Build and return the SponsorshipSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SponsorshipSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SponsorshipSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
