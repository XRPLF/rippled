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

class SponsoredAccountCreateBuilder;

/**
 * @brief Transaction: SponsoredAccountCreate
 *
 * Type: ttSPONSORED_ACCOUNT_CREATE (96)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureSponsorV1_1
 * Privileges: Privilege::CreateAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SponsoredAccountCreateBuilder to construct new transactions.
 */
class SponsoredAccountCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSPONSORED_ACCOUNT_CREATE;

    /**
     * @brief Construct a SponsoredAccountCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SponsoredAccountCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SponsoredAccountCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDestination (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_->at(sfDestination);
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
 * @brief Builder for SponsoredAccountCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SponsoredAccountCreateBuilder : public TransactionBuilderBase<SponsoredAccountCreateBuilder>
{
public:
    /**
     * @brief Construct a new SponsoredAccountCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SponsoredAccountCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SponsoredAccountCreateBuilder>(ttSPONSORED_ACCOUNT_CREATE, account, sequence, fee)
    {
        setDestination(destination);
    }

    /**
     * @brief Construct a SponsoredAccountCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SponsoredAccountCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSPONSORED_ACCOUNT_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for SponsoredAccountCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    SponsoredAccountCreateBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsoredAccountCreateBuilder&
    setFeeAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfFeeAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMaxFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsoredAccountCreateBuilder&
    setMaxFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMaxFee] = value;
        return *this;
    }

    /**
     * @brief Set sfRemainingOwnerCount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SponsoredAccountCreateBuilder&
    setRemainingOwnerCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfRemainingOwnerCount] = value;
        return *this;
    }

    /**
     * @brief Build and return the SponsoredAccountCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SponsoredAccountCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SponsoredAccountCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
