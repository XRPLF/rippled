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
class LedgerStateFixBuilder;

/**
 * Transaction: LedgerStateFix
 * Type: ttLEDGER_STATE_FIX (53)
 * Delegable: Delegation::delegable
 * Amendment: fixNFTokenPageLinks
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LedgerStateFixBuilder to construct new transactions.
 */
class LedgerStateFix : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLEDGER_STATE_FIX;

    /**
     * Construct a LedgerStateFix transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LedgerStateFix(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LedgerStateFix");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfLedgerFixType (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getLedgerFixType() const
    {
        return this->tx_.at(sfLedgerFixType);
    }

    /**
     * Get sfOwner (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_.at(sfOwner);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_.isFieldPresent(sfOwner);
    }
};

/**
 * Builder for LedgerStateFix transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LedgerStateFixBuilder : public TransactionBuilderBase<LedgerStateFixBuilder>
{
public:
    LedgerStateFixBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT16::type::value_type> const& ledgerFixType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LedgerStateFixBuilder>(ttLEDGER_STATE_FIX, account, sequence, fee)
    {
        setLedgerFixType(ledgerFixType);
    }

    LedgerStateFixBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLEDGER_STATE_FIX)
        {
            throw std::runtime_error("Invalid transaction type for LedgerStateFixBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLedgerFixType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LedgerStateFixBuilder&
    setLedgerFixType(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfLedgerFixType] = value;
        return *this;
    }

    /**
     * Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LedgerStateFixBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Build and return the completed LedgerStateFix wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LedgerStateFix
    build()
    {
        return LedgerStateFix(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
