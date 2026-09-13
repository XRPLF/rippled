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

class CouponScheduleCreateBuilder;

/**
 * @brief Transaction: CouponScheduleCreate
 *
 * Type: ttCOUPON_SCHEDULE_CREATE (92)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCouponPayments
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CouponScheduleCreateBuilder to construct new transactions.
 */
class CouponScheduleCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCOUPON_SCHEDULE_CREATE;

    /**
     * @brief Construct a CouponScheduleCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CouponScheduleCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMPTokenIssuanceID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_->at(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfCouponAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getCouponAsset() const
    {
        return this->tx_->at(sfCouponAsset);
    }

    /**
     * @brief Get sfCouponAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getCouponAmount() const
    {
        if (hasCouponAmount())
        {
            return this->tx_->at(sfCouponAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCouponAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCouponAmount() const
    {
        return this->tx_->isFieldPresent(sfCouponAmount);
    }

    /**
     * @brief Get sfCouponInterval (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCouponInterval() const
    {
        if (hasCouponInterval())
        {
            return this->tx_->at(sfCouponInterval);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCouponInterval is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCouponInterval() const
    {
        return this->tx_->isFieldPresent(sfCouponInterval);
    }

    /**
     * @brief Get sfFirstCouponTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFirstCouponTime() const
    {
        if (hasFirstCouponTime())
        {
            return this->tx_->at(sfFirstCouponTime);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFirstCouponTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFirstCouponTime() const
    {
        return this->tx_->isFieldPresent(sfFirstCouponTime);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_->at(sfExpiration);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfCallNoticePeriod (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCallNoticePeriod() const
    {
        if (hasCallNoticePeriod())
        {
            return this->tx_->at(sfCallNoticePeriod);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCallNoticePeriod is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCallNoticePeriod() const
    {
        return this->tx_->isFieldPresent(sfCallNoticePeriod);
    }

    /**
     * @brief Get sfEarliestCallTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getEarliestCallTime() const
    {
        if (hasEarliestCallTime())
        {
            return this->tx_->at(sfEarliestCallTime);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfEarliestCallTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEarliestCallTime() const
    {
        return this->tx_->isFieldPresent(sfEarliestCallTime);
    }
};

/**
 * @brief Builder for CouponScheduleCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CouponScheduleCreateBuilder : public TransactionBuilderBase<CouponScheduleCreateBuilder>
{
public:
    /**
     * @brief Construct a new CouponScheduleCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param couponAsset The sfCouponAsset field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CouponScheduleCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_ISSUE::type::value_type> const& couponAsset,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CouponScheduleCreateBuilder>(ttCOUPON_SCHEDULE_CREATE, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setCouponAsset(couponAsset);
    }

    /**
     * @brief Construct a CouponScheduleCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CouponScheduleCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCOUPON_SCHEDULE_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMPTokenIssuanceID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponAsset (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setCouponAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfCouponAsset] = STIssue(sfCouponAsset, value);
        return *this;
    }

    /**
     * @brief Set sfCouponAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setCouponAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfCouponAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponInterval (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setCouponInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCouponInterval] = value;
        return *this;
    }

    /**
     * @brief Set sfFirstCouponTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setFirstCouponTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFirstCouponTime] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfCallNoticePeriod (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setCallNoticePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCallNoticePeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfEarliestCallTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleCreateBuilder&
    setEarliestCallTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfEarliestCallTime] = value;
        return *this;
    }

    /**
     * @brief Build and return the CouponScheduleCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CouponScheduleCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CouponScheduleCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
