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

class CouponScheduleSetBuilder;

/**
 * @brief Transaction: CouponScheduleSet
 *
 * Type: ttCOUPON_SCHEDULE_SET (93)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCouponPayments
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CouponScheduleSetBuilder to construct new transactions.
 */
class CouponScheduleSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCOUPON_SCHEDULE_SET;

    /**
     * @brief Construct a CouponScheduleSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CouponScheduleSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleSet");
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
     * @brief Get sfExpiration (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getExpiration() const
    {
        return this->tx_->at(sfExpiration);
    }
};

/**
 * @brief Builder for CouponScheduleSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CouponScheduleSetBuilder : public TransactionBuilderBase<CouponScheduleSetBuilder>
{
public:
    /**
     * @brief Construct a new CouponScheduleSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param expiration The sfExpiration field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CouponScheduleSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                     std::decay_t<typename SF_UINT32::type::value_type> const& expiration,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CouponScheduleSetBuilder>(ttCOUPON_SCHEDULE_SET, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setExpiration(expiration);
    }

    /**
     * @brief Construct a CouponScheduleSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CouponScheduleSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCOUPON_SCHEDULE_SET)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleSetBuilder");
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
    CouponScheduleSetBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleSetBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Build and return the CouponScheduleSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CouponScheduleSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CouponScheduleSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
