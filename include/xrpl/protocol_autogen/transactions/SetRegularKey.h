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

// Forward declaration
class SetRegularKeyBuilder;

/**
 * Transaction: SetRegularKey
 * Type: ttREGULAR_KEY_SET (5)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SetRegularKeyBuilder to construct new transactions.
 */
class SetRegularKey : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttREGULAR_KEY_SET;

    /**
     * Construct a SetRegularKey transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SetRegularKey(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SetRegularKey");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfRegularKey (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getRegularKey() const
    {
        if (hasRegularKey())
        {
            return this->tx_->at(sfRegularKey);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasRegularKey() const
    {
        return this->tx_->isFieldPresent(sfRegularKey);
    }
};

/**
 * Builder for SetRegularKey transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SetRegularKeyBuilder : public TransactionBuilderBase<SetRegularKeyBuilder>
{
public:
    SetRegularKeyBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SetRegularKeyBuilder>(ttREGULAR_KEY_SET, account, sequence, fee)
    {
    }

    SetRegularKeyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttREGULAR_KEY_SET)
        {
            throw std::runtime_error("Invalid transaction type for SetRegularKeyBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfRegularKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetRegularKeyBuilder&
    setRegularKey(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfRegularKey] = value;
        return *this;
    }

    /**
     * Build and return the SetRegularKey wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    SetRegularKey
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SetRegularKey{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
