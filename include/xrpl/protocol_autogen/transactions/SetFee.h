// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class SetFeeBuilder;

/**
 * Transaction: SetFee
 * Type: ttFEE (101)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SetFeeBuilder to construct new transactions.
 */
class SetFee : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttFEE;

    /**
     * Construct a SetFee transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SetFee(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SetFee");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfLedgerSequence (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLedgerSequence() const
    {
        if (hasLedgerSequence())
        {
            return this->tx_.at(sfLedgerSequence);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLedgerSequence() const
    {
        return this->tx_.isFieldPresent(sfLedgerSequence);
    }

    /**
     * Get sfBaseFee (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getBaseFee() const
    {
        if (hasBaseFee())
        {
            return this->tx_.at(sfBaseFee);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBaseFee() const
    {
        return this->tx_.isFieldPresent(sfBaseFee);
    }

    /**
     * Get sfReferenceFeeUnits (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReferenceFeeUnits() const
    {
        if (hasReferenceFeeUnits())
        {
            return this->tx_.at(sfReferenceFeeUnits);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReferenceFeeUnits() const
    {
        return this->tx_.isFieldPresent(sfReferenceFeeUnits);
    }

    /**
     * Get sfReserveBase (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveBase() const
    {
        if (hasReserveBase())
        {
            return this->tx_.at(sfReserveBase);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveBase() const
    {
        return this->tx_.isFieldPresent(sfReserveBase);
    }

    /**
     * Get sfReserveIncrement (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveIncrement() const
    {
        if (hasReserveIncrement())
        {
            return this->tx_.at(sfReserveIncrement);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveIncrement() const
    {
        return this->tx_.isFieldPresent(sfReserveIncrement);
    }

    /**
     * Get sfBaseFeeDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBaseFeeDrops() const
    {
        if (hasBaseFeeDrops())
        {
            return this->tx_.at(sfBaseFeeDrops);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBaseFeeDrops() const
    {
        return this->tx_.isFieldPresent(sfBaseFeeDrops);
    }

    /**
     * Get sfReserveBaseDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveBaseDrops() const
    {
        if (hasReserveBaseDrops())
        {
            return this->tx_.at(sfReserveBaseDrops);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveBaseDrops() const
    {
        return this->tx_.isFieldPresent(sfReserveBaseDrops);
    }

    /**
     * Get sfReserveIncrementDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveIncrementDrops() const
    {
        if (hasReserveIncrementDrops())
        {
            return this->tx_.at(sfReserveIncrementDrops);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveIncrementDrops() const
    {
        return this->tx_.isFieldPresent(sfReserveIncrementDrops);
    }
};

/**
 * Builder for SetFee transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SetFeeBuilder : public TransactionBuilderBase<SetFeeBuilder>
{
public:
    SetFeeBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SetFeeBuilder>(ttFEE, account, sequence, fee)
    {
    }

    SetFeeBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttFEE)
        {
            throw std::runtime_error("Invalid transaction type for SetFeeBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLedgerSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * Set sfBaseFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setBaseFee(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBaseFee] = value;
        return *this;
    }

    /**
     * Set sfReferenceFeeUnits (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReferenceFeeUnits(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReferenceFeeUnits] = value;
        return *this;
    }

    /**
     * Set sfReserveBase (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveBase(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveBase] = value;
        return *this;
    }

    /**
     * Set sfReserveIncrement (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveIncrement(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveIncrement] = value;
        return *this;
    }

    /**
     * Set sfBaseFeeDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setBaseFeeDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBaseFeeDrops] = value;
        return *this;
    }

    /**
     * Set sfReserveBaseDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveBaseDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveBaseDrops] = value;
        return *this;
    }

    /**
     * Set sfReserveIncrementDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveIncrementDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveIncrementDrops] = value;
        return *this;
    }

    /**
     * Build and return the completed SetFee wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, SetFee>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, SetFee>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
