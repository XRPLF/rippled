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

class SetFeeBuilder;

/**
 * @brief Transaction: SetFee
 *
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
     * @brief Construct a SetFee transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SetFee(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SetFee");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLedgerSequence (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLedgerSequence() const
    {
        if (hasLedgerSequence())
        {
            return this->tx_->at(sfLedgerSequence);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLedgerSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLedgerSequence() const
    {
        return this->tx_->isFieldPresent(sfLedgerSequence);
    }

    /**
     * @brief Get sfBaseFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getBaseFee() const
    {
        if (hasBaseFee())
        {
            return this->tx_->at(sfBaseFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBaseFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBaseFee() const
    {
        return this->tx_->isFieldPresent(sfBaseFee);
    }

    /**
     * @brief Get sfReferenceFeeUnits (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReferenceFeeUnits() const
    {
        if (hasReferenceFeeUnits())
        {
            return this->tx_->at(sfReferenceFeeUnits);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfReferenceFeeUnits is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReferenceFeeUnits() const
    {
        return this->tx_->isFieldPresent(sfReferenceFeeUnits);
    }

    /**
     * @brief Get sfReserveBase (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveBase() const
    {
        if (hasReserveBase())
        {
            return this->tx_->at(sfReserveBase);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveBase is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveBase() const
    {
        return this->tx_->isFieldPresent(sfReserveBase);
    }

    /**
     * @brief Get sfReserveIncrement (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveIncrement() const
    {
        if (hasReserveIncrement())
        {
            return this->tx_->at(sfReserveIncrement);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveIncrement is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveIncrement() const
    {
        return this->tx_->isFieldPresent(sfReserveIncrement);
    }

    /**
     * @brief Get sfBaseFeeDrops (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBaseFeeDrops() const
    {
        if (hasBaseFeeDrops())
        {
            return this->tx_->at(sfBaseFeeDrops);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBaseFeeDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBaseFeeDrops() const
    {
        return this->tx_->isFieldPresent(sfBaseFeeDrops);
    }

    /**
     * @brief Get sfReserveBaseDrops (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveBaseDrops() const
    {
        if (hasReserveBaseDrops())
        {
            return this->tx_->at(sfReserveBaseDrops);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveBaseDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveBaseDrops() const
    {
        return this->tx_->isFieldPresent(sfReserveBaseDrops);
    }

    /**
     * @brief Get sfReserveIncrementDrops (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveIncrementDrops() const
    {
        if (hasReserveIncrementDrops())
        {
            return this->tx_->at(sfReserveIncrementDrops);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveIncrementDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveIncrementDrops() const
    {
        return this->tx_->isFieldPresent(sfReserveIncrementDrops);
    }
};

/**
 * @brief Builder for SetFee transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SetFeeBuilder : public TransactionBuilderBase<SetFeeBuilder>
{
public:
    /**
     * @brief Construct a new SetFeeBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SetFeeBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SetFeeBuilder>(ttFEE, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a SetFeeBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SetFeeBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttFEE)
        {
            throw std::runtime_error("Invalid transaction type for SetFeeBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLedgerSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfBaseFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setBaseFee(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBaseFee] = value;
        return *this;
    }

    /**
     * @brief Set sfReferenceFeeUnits (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReferenceFeeUnits(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReferenceFeeUnits] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveBase (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveBase(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveBase] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveIncrement (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveIncrement(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveIncrement] = value;
        return *this;
    }

    /**
     * @brief Set sfBaseFeeDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setBaseFeeDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBaseFeeDrops] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveBaseDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveBaseDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveBaseDrops] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveIncrementDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SetFeeBuilder&
    setReserveIncrementDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveIncrementDrops] = value;
        return *this;
    }

    /**
     * @brief Build and return the SetFee wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SetFee
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SetFee{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
