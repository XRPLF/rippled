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

class EscrowFinishBuilder;

/**
 * @brief Transaction: EscrowFinish
 *
 * Type: ttESCROW_FINISH (2)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EscrowFinishBuilder to construct new transactions.
 */
class EscrowFinish : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttESCROW_FINISH;

    /**
     * @brief Construct a EscrowFinish transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EscrowFinish(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EscrowFinish");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOwner (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->tx_->at(sfOwner);
    }

    /**
     * @brief Get sfOfferSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOfferSequence() const
    {
        return this->tx_->at(sfOfferSequence);
    }

    /**
     * @brief Get sfFulfillment (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getFulfillment() const
    {
        if (hasFulfillment())
        {
            return this->tx_->at(sfFulfillment);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFulfillment is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFulfillment() const
    {
        return this->tx_->isFieldPresent(sfFulfillment);
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
     * @brief Get sfCredentialIDs (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_->at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCredentialIDs is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }
};

/**
 * @brief Builder for EscrowFinish transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EscrowFinishBuilder : public TransactionBuilderBase<EscrowFinishBuilder>
{
public:
    /**
     * @brief Construct a new EscrowFinishBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param owner The sfOwner field value.
     * @param offerSequence The sfOfferSequence field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    EscrowFinishBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<EscrowFinishBuilder>(ttESCROW_FINISH, account, sequence, fee)
    {
        setOwner(owner);
        setOfferSequence(offerSequence);
    }

    /**
     * @brief Construct a EscrowFinishBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    EscrowFinishBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttESCROW_FINISH)
        {
            throw std::runtime_error("Invalid transaction type for EscrowFinishBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfFulfillment (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setFulfillment(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfFulfillment] = value;
        return *this;
    }

    /**
     * @brief Set sfCondition (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setCondition(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCondition] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * @brief Build and return the EscrowFinish wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    EscrowFinish
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return EscrowFinish{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
