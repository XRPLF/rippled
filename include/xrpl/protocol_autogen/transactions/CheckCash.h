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

class CheckCashBuilder;

/**
 * @brief Transaction: CheckCash
 *
 * Type: ttCHECK_CASH (17)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CheckCashBuilder to construct new transactions.
 */
class CheckCash : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCHECK_CASH;

    /**
     * @brief Construct a CheckCash transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CheckCash(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CheckCash");
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

    /**
     * @brief Get sfAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }

    /**
     * @brief Get sfDeliverMin (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getDeliverMin() const
    {
        if (hasDeliverMin())
        {
            return this->tx_->at(sfDeliverMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDeliverMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDeliverMin() const
    {
        return this->tx_->isFieldPresent(sfDeliverMin);
    }
};

/**
 * @brief Builder for CheckCash transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CheckCashBuilder : public TransactionBuilderBase<CheckCashBuilder>
{
public:
    /**
     * @brief Construct a new CheckCashBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param checkID The sfCheckID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CheckCashBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& checkID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CheckCashBuilder>(ttCHECK_CASH, account, sequence, fee)
    {
        setCheckID(checkID);
    }

    /**
     * @brief Construct a CheckCashBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CheckCashBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCHECK_CASH)
        {
            throw std::runtime_error("Invalid transaction type for CheckCashBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfCheckID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setCheckID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfCheckID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfDeliverMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setDeliverMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfDeliverMin] = value;
        return *this;
    }

    /**
     * @brief Build and return the CheckCash wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CheckCash
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CheckCash{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
