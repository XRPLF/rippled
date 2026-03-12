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

class TicketCreateBuilder;

/**
 * @brief Transaction: TicketCreate
 *
 * Type: ttTICKET_CREATE (10)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TicketCreateBuilder to construct new transactions.
 */
class TicketCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTICKET_CREATE;

    /**
     * @brief Construct a TicketCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TicketCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TicketCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfTicketCount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getTicketCount() const
    {
        return this->tx_->at(sfTicketCount);
    }
};

/**
 * @brief Builder for TicketCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TicketCreateBuilder : public TransactionBuilderBase<TicketCreateBuilder>
{
public:
    /**
     * @brief Construct a new TicketCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param ticketCount The sfTicketCount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TicketCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& ticketCount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TicketCreateBuilder>(ttTICKET_CREATE, account, sequence, fee)
    {
        setTicketCount(ticketCount);
    }

    /**
     * @brief Construct a TicketCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TicketCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTICKET_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for TicketCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfTicketCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    TicketCreateBuilder&
    setTicketCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTicketCount] = value;
        return *this;
    }

    /**
     * @brief Build and return the TicketCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TicketCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TicketCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
