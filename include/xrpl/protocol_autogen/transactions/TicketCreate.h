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

// Forward declaration
class TicketCreateBuilder;

/**
 * Transaction: TicketCreate
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
     * Construct a TicketCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TicketCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TicketCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfTicketCount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getTicketCount() const
    {
        return this->tx_.at(sfTicketCount);
    }
};

/**
 * Builder for TicketCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TicketCreateBuilder : public TransactionBuilderBase<TicketCreateBuilder>
{
public:
    TicketCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& ticketCount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TicketCreateBuilder>(ttTICKET_CREATE, account, sequence, fee)
    {
        setTicketCount(ticketCount);
    }

    TicketCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttTICKET_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for TicketCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfTicketCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    TicketCreateBuilder&
    setTicketCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTicketCount] = value;
        return *this;
    }

    /**
     * Build and return the completed TicketCreate wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    TicketCreate
    build()
    {
        return TicketCreate(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
