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

class CouponScheduleDeleteBuilder;

/**
 * @brief Transaction: CouponScheduleDelete
 *
 * Type: ttCOUPON_SCHEDULE_DELETE (94)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCouponPayments
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CouponScheduleDeleteBuilder to construct new transactions.
 */
class CouponScheduleDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCOUPON_SCHEDULE_DELETE;

    /**
     * @brief Construct a CouponScheduleDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CouponScheduleDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleDelete");
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
};

/**
 * @brief Builder for CouponScheduleDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CouponScheduleDeleteBuilder : public TransactionBuilderBase<CouponScheduleDeleteBuilder>
{
public:
    /**
     * @brief Construct a new CouponScheduleDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CouponScheduleDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CouponScheduleDeleteBuilder>(ttCOUPON_SCHEDULE_DELETE, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    /**
     * @brief Construct a CouponScheduleDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CouponScheduleDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCOUPON_SCHEDULE_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for CouponScheduleDeleteBuilder");
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
    CouponScheduleDeleteBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Build and return the CouponScheduleDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CouponScheduleDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CouponScheduleDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
