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
class CheckCashBuilder;

/**
 * Transaction: CheckCash
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
     * Construct a CheckCash transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CheckCash(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CheckCash");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfCheckID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getCheckID() const
    {
        return this->tx_.at(sfCheckID);
    }

    /**
     * Get sfAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_.at(sfAmount);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_.isFieldPresent(sfAmount);
    }

    /**
     * Get sfDeliverMin (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getDeliverMin() const
    {
        if (hasDeliverMin())
        {
            return this->tx_.at(sfDeliverMin);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDeliverMin() const
    {
        return this->tx_.isFieldPresent(sfDeliverMin);
    }
};

/**
 * Builder for CheckCash transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CheckCashBuilder : public TransactionBuilderBase<CheckCashBuilder>
{
public:
    CheckCashBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& checkID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CheckCashBuilder>(ttCHECK_CASH, account, sequence, fee)
    {
        setCheckID(checkID);
    }

    CheckCashBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCHECK_CASH)
        {
            throw std::runtime_error("Invalid transaction type for CheckCashBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfCheckID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setCheckID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfCheckID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfDeliverMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckCashBuilder&
    setDeliverMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfDeliverMin] = value;
        return *this;
    }

    /**
     * Build and return the completed CheckCash wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    CheckCash
    build()
    {
        return CheckCash(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
