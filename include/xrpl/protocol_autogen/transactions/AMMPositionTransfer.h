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

class AMMPositionTransferBuilder;

/**
 * @brief Transaction: AMMPositionTransfer
 *
 * Type: ttAMM_POSITION_TRANSFER (113)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMMCurves
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMPositionTransferBuilder to construct new transactions.
 */
class AMMPositionTransfer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_POSITION_TRANSFER;

    /**
     * @brief Construct a AMMPositionTransfer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMPositionTransfer(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMPositionTransfer");
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
     * @brief Get sfPositionID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPositionID() const
    {
        return this->tx_->at(sfPositionID);
    }
};

/**
 * @brief Builder for AMMPositionTransfer transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMPositionTransferBuilder : public TransactionBuilderBase<AMMPositionTransferBuilder>
{
public:
    /**
     * @brief Construct a new AMMPositionTransferBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param positionID The sfPositionID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMPositionTransferBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_UINT256::type::value_type> const& positionID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMPositionTransferBuilder>(ttAMM_POSITION_TRANSFER, account, sequence, fee)
    {
        setDestination(destination);
        setPositionID(positionID);
    }

    /**
     * @brief Construct a AMMPositionTransferBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMPositionTransferBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_POSITION_TRANSFER)
        {
            throw std::runtime_error("Invalid transaction type for AMMPositionTransferBuilder");
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
    AMMPositionTransferBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfPositionID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionTransferBuilder&
    setPositionID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPositionID] = value;
        return *this;
    }

    /**
     * @brief Build and return the AMMPositionTransfer wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMPositionTransfer
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMPositionTransfer{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
