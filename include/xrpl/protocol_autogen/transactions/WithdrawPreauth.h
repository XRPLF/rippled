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

class WithdrawPreauthBuilder;

/**
 * @brief Transaction: WithdrawPreauth
 *
 * Type: ttWITHDRAW_PREAUTH (103)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureFirewall
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use WithdrawPreauthBuilder to construct new transactions.
 */
class WithdrawPreauth : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttWITHDRAW_PREAUTH;

    /**
     * @brief Construct a WithdrawPreauth transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit WithdrawPreauth(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for WithdrawPreauth");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAuthorize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAuthorize() const
    {
        if (hasAuthorize())
        {
            return this->tx_->at(sfAuthorize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuthorize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuthorize() const
    {
        return this->tx_->isFieldPresent(sfAuthorize);
    }

    /**
     * @brief Get sfUnauthorize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getUnauthorize() const
    {
        if (hasUnauthorize())
        {
            return this->tx_->at(sfUnauthorize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfUnauthorize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasUnauthorize() const
    {
        return this->tx_->isFieldPresent(sfUnauthorize);
    }

    /**
     * @brief Get sfDestinationTag (SoeOptional)
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
    /**
     * @brief Get sfCounterpartySignature (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STObject
    getCounterpartySignature() const
    {
        return this->tx_->getFieldObject(sfCounterpartySignature);
    }

    /**
     * @brief Get sfFirewallID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getFirewallID() const
    {
        return this->tx_->at(sfFirewallID);
    }
};

/**
 * @brief Builder for WithdrawPreauth transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class WithdrawPreauthBuilder : public TransactionBuilderBase<WithdrawPreauthBuilder>
{
public:
    /**
     * @brief Construct a new WithdrawPreauthBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param counterpartySignature The sfCounterpartySignature field value.
     * @param firewallID The sfFirewallID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    WithdrawPreauthBuilder(SF_ACCOUNT::type::value_type account,
                     STObject const& counterpartySignature,                     std::decay_t<typename SF_UINT256::type::value_type> const& firewallID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<WithdrawPreauthBuilder>(ttWITHDRAW_PREAUTH, account, sequence, fee)
    {
        setCounterpartySignature(counterpartySignature);
        setFirewallID(firewallID);
    }

    /**
     * @brief Construct a WithdrawPreauthBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    WithdrawPreauthBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttWITHDRAW_PREAUTH)
        {
            throw std::runtime_error("Invalid transaction type for WithdrawPreauthBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAuthorize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfUnauthorize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setUnauthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfUnauthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterpartySignature (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Set sfFirewallID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setFirewallID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfFirewallID] = value;
        return *this;
    }

    /**
     * @brief Build and return the WithdrawPreauth wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    WithdrawPreauth
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return WithdrawPreauth{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
