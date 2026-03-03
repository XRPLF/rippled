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
class CheckCreateBuilder;

/**
 * Transaction: CheckCreate
 * Type: ttCHECK_CREATE (16)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CheckCreateBuilder to construct new transactions.
 */
class CheckCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCHECK_CREATE;

    /**
     * Construct a CheckCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CheckCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CheckCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_.at(sfDestination);
    }

    /**
     * Get sfSendMax (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSendMax() const
    {
        return this->tx_.at(sfSendMax);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_.at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfDestinationTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_.at(sfDestinationTag);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_.isFieldPresent(sfDestinationTag);
    }

    /**
     * Get sfInvoiceID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getInvoiceID() const
    {
        if (hasInvoiceID())
        {
            return this->tx_.at(sfInvoiceID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasInvoiceID() const
    {
        return this->tx_.isFieldPresent(sfInvoiceID);
    }
};

/**
 * Builder for CheckCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CheckCreateBuilder : public TransactionBuilderBase<CheckCreateBuilder>
{
public:
    CheckCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& sendMax,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CheckCreateBuilder>(ttCHECK_CREATE, account, sequence, fee)
    {
        setDestination(destination);
        setSendMax(sendMax);
    }

    CheckCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCHECK_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for CheckCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckCreateBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfSendMax (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckCreateBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCreateBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Set sfInvoiceID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCreateBuilder&
    setInvoiceID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfInvoiceID] = value;
        return *this;
    }

    /**
     * Build and return the completed CheckCreate wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    CheckCreate
    build()
    {
        return CheckCreate(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
