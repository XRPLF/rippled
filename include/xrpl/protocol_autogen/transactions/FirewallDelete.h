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

class FirewallDeleteBuilder;

/**
 * @brief Transaction: FirewallDelete
 *
 * Type: ttFIREWALL_DELETE (105)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureFirewall
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use FirewallDeleteBuilder to construct new transactions.
 */
class FirewallDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttFIREWALL_DELETE;

    /**
     * @brief Construct a FirewallDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit FirewallDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for FirewallDelete");
        }
    }

    // Transaction-specific field getters
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
 * @brief Builder for FirewallDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class FirewallDeleteBuilder : public TransactionBuilderBase<FirewallDeleteBuilder>
{
public:
    /**
     * @brief Construct a new FirewallDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param counterpartySignature The sfCounterpartySignature field value.
     * @param firewallID The sfFirewallID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    FirewallDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     STObject const& counterpartySignature,                     std::decay_t<typename SF_UINT256::type::value_type> const& firewallID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<FirewallDeleteBuilder>(ttFIREWALL_DELETE, account, sequence, fee)
    {
        setCounterpartySignature(counterpartySignature);
        setFirewallID(firewallID);
    }

    /**
     * @brief Construct a FirewallDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    FirewallDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttFIREWALL_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for FirewallDeleteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCounterpartySignature (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallDeleteBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Set sfFirewallID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallDeleteBuilder&
    setFirewallID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfFirewallID] = value;
        return *this;
    }

    /**
     * @brief Build and return the FirewallDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    FirewallDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return FirewallDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
