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

class EscrowCreateBuilder;

/**
 * @brief Transaction: EscrowCreate
 *
 * Type: ttESCROW_CREATE (1)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EscrowCreateBuilder to construct new transactions.
 */
class EscrowCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttESCROW_CREATE;

    /**
     * @brief Construct a EscrowCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EscrowCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDestination (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_->at(sfDestination);
    }

    /**
     * @brief Get sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * @brief Get sfCondition (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getCondition() const
    {
        if (hasCondition())
        {
            return this->tx_->at(sfCondition);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCondition is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCondition() const
    {
        return this->tx_->isFieldPresent(sfCondition);
    }

    /**
     * @brief Get sfCancelAfter (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
        {
            return this->tx_->at(sfCancelAfter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCancelAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->tx_->isFieldPresent(sfCancelAfter);
    }

    /**
     * @brief Get sfFinishAfter (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFinishAfter() const
    {
        if (hasFinishAfter())
        {
            return this->tx_->at(sfFinishAfter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFinishAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFinishAfter() const
    {
        return this->tx_->isFieldPresent(sfFinishAfter);
    }

    /**
     * @brief Get sfDestinationTag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_->at(sfDestinationTag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_->isFieldPresent(sfDestinationTag);
    }
};

/**
 * @brief Builder for EscrowCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EscrowCreateBuilder : public TransactionBuilderBase<EscrowCreateBuilder>
{
public:
    /**
     * @brief Construct a new EscrowCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    EscrowCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<EscrowCreateBuilder>(ttESCROW_CREATE, account, sequence, fee)
    {
        setDestination(destination);
        setAmount(amount);
    }

    /**
     * @brief Construct a EscrowCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    EscrowCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttESCROW_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfCondition (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setCondition(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCondition] = value;
        return *this;
    }

    /**
     * @brief Set sfCancelAfter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfFinishAfter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setFinishAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFinishAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowCreateBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Build and return the EscrowCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    EscrowCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return EscrowCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
