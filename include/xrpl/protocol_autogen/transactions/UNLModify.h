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
class UNLModifyBuilder;

/**
 * Transaction: UNLModify
 * Type: ttUNL_MODIFY (102)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use UNLModifyBuilder to construct new transactions.
 */
class UNLModify : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttUNL_MODIFY;

    /**
     * Construct a UNLModify transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit UNLModify(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for UNLModify");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfUNLModifyDisabling (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getUNLModifyDisabling() const
    {
        return this->tx_.at(sfUNLModifyDisabling);
    }

    /**
     * Get sfLedgerSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLedgerSequence() const
    {
        return this->tx_.at(sfLedgerSequence);
    }

    /**
     * Get sfUNLModifyValidator (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getUNLModifyValidator() const
    {
        return this->tx_.at(sfUNLModifyValidator);
    }
};

/**
 * Builder for UNLModify transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class UNLModifyBuilder : public TransactionBuilderBase<UNLModifyBuilder>
{
public:
    UNLModifyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT8::type::value_type> const& uNLModifyDisabling,                     std::decay_t<typename SF_UINT32::type::value_type> const& ledgerSequence,                     std::decay_t<typename SF_VL::type::value_type> const& uNLModifyValidator,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<UNLModifyBuilder>(ttUNL_MODIFY, account, sequence, fee)
    {
        setUNLModifyDisabling(uNLModifyDisabling);
        setLedgerSequence(ledgerSequence);
        setUNLModifyValidator(uNLModifyValidator);
    }

    UNLModifyBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttUNL_MODIFY)
        {
            throw std::runtime_error("Invalid transaction type for UNLModifyBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfUNLModifyDisabling (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setUNLModifyDisabling(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfUNLModifyDisabling] = value;
        return *this;
    }

    /**
     * Set sfLedgerSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * Set sfUNLModifyValidator (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setUNLModifyValidator(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfUNLModifyValidator] = value;
        return *this;
    }

    /**
     * Build and return the completed UNLModify wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    UNLModify
    build()
    {
        return UNLModify(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
