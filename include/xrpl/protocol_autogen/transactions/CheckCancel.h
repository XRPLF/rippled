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

class CheckCancelBuilder;

/**
 * @brief Transaction: CheckCancel
 *
 * Type: ttCHECK_CANCEL (18)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CheckCancelBuilder to construct new transactions.
 */
class CheckCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCHECK_CANCEL;

    /**
     * @brief Construct a CheckCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CheckCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CheckCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCheckID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getCheckID() const
    {
        return this->tx_->at(sfCheckID);
    }
};

/**
 * @brief Builder for CheckCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CheckCancelBuilder : public TransactionBuilderBase<CheckCancelBuilder>
{
public:
    /**
     * @brief Construct a new CheckCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param checkID The sfCheckID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CheckCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& checkID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CheckCancelBuilder>(ttCHECK_CANCEL, account, sequence, fee)
    {
        setCheckID(checkID);
    }

    /**
     * @brief Construct a CheckCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CheckCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCHECK_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for CheckCancelBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfCheckID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckCancelBuilder&
    setCheckID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfCheckID] = value;
        return *this;
    }

    /**
     * @brief Build and return the CheckCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CheckCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CheckCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
